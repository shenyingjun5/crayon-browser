#[cfg(target_os = "macos")]
mod macos {
    use crayon_app_runtime::content_host_runtime::ContentHostRuntime;
    use crayon_ipc_schema::{
        decode_content_host_message, encode_content_host_message, ContentHostMessage,
        MAX_CONTENT_HOST_FRAME_BYTES,
    };
    use std::fs::{self, Permissions};
    use std::io::{self, Read, Write};
    use std::os::unix::fs::{FileTypeExt, PermissionsExt};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::thread::{self, JoinHandle};
    use std::time::Duration;

    const HEALTH_REQUEST: &[u8; 4] = b"PING";
    const HEALTH_REPLY: &[u8; 4] = b"PONG";
    const HEALTH_IO_TIMEOUT: Duration = Duration::from_millis(250);

    pub fn run() -> Result<(), HostProcessError> {
        let socket_path = arguments()?;
        let health = HealthServer::start(socket_path)?;
        let stdin = io::stdin();
        let stdout = io::stdout();
        let mut input = stdin.lock();
        let mut output = stdout.lock();
        let mut runtime = ContentHostRuntime::default();
        loop {
            let payload = read_frame(&mut input)?.ok_or(HostProcessError::UnexpectedEof)?;
            let message = decode_content_host_message(&payload)
                .map_err(|_| HostProcessError::InvalidMessage)?;
            let request_id = request_id(&message).map(str::to_owned);
            let shutdown = matches!(message, ContentHostMessage::Shutdown);
            match runtime.handle(message) {
                Ok(replies) => write_replies(&mut output, replies)?,
                Err(error) => {
                    let request_id = request_id.ok_or(HostProcessError::InvalidState)?;
                    write_replies(
                        &mut output,
                        vec![ContentHostMessage::ErrorReply {
                            request_id,
                            code: error.reply_code(),
                        }],
                    )?;
                }
            }
            if shutdown {
                break;
            }
        }
        drop(health);
        Ok(())
    }

    fn arguments() -> Result<PathBuf, HostProcessError> {
        let mut arguments = std::env::args_os().skip(1);
        if arguments.next().as_deref() != Some(std::ffi::OsStr::new("--health-socket")) {
            return Err(HostProcessError::InvalidArguments);
        }
        let path = PathBuf::from(arguments.next().ok_or(HostProcessError::InvalidArguments)?);
        if arguments.next().is_some() || !path.is_absolute() || path.exists() {
            return Err(HostProcessError::InvalidArguments);
        }
        let parent = path.parent().ok_or(HostProcessError::InvalidArguments)?;
        let metadata = fs::metadata(parent).map_err(|_| HostProcessError::InvalidArguments)?;
        if !metadata.is_dir() || metadata.permissions().mode() & 0o077 != 0 {
            return Err(HostProcessError::InvalidArguments);
        }
        Ok(path)
    }

    fn request_id(message: &ContentHostMessage) -> Option<&str> {
        match message {
            ContentHostMessage::Begin { request_id, .. }
            | ContentHostMessage::FactBatch { request_id, .. }
            | ContentHostMessage::Terminal { request_id, .. }
            | ContentHostMessage::Cancel { request_id }
            | ContentHostMessage::MarkdownChunk { request_id, .. }
            | ContentHostMessage::ErrorReply { request_id, .. } => Some(request_id),
            ContentHostMessage::Navigation { .. }
            | ContentHostMessage::CloseTab { .. }
            | ContentHostMessage::Shutdown => None,
        }
    }

    fn read_frame(reader: &mut impl Read) -> Result<Option<Vec<u8>>, HostProcessError> {
        let mut header = [0u8; 4];
        let mut offset = 0;
        while offset < header.len() {
            let read = reader.read(&mut header[offset..])?;
            if read == 0 {
                return if offset == 0 {
                    Ok(None)
                } else {
                    Err(HostProcessError::TruncatedFrame)
                };
            }
            offset += read;
        }
        let length = u32::from_be_bytes(header) as usize;
        if length > MAX_CONTENT_HOST_FRAME_BYTES {
            return Err(HostProcessError::FrameTooLarge);
        }
        let mut payload = vec![0; length];
        reader
            .read_exact(&mut payload)
            .map_err(|_| HostProcessError::TruncatedFrame)?;
        Ok(Some(payload))
    }

    fn write_replies(
        writer: &mut impl Write,
        replies: Vec<ContentHostMessage>,
    ) -> Result<(), HostProcessError> {
        for reply in replies {
            let payload = encode_content_host_message(&reply)
                .map_err(|_| HostProcessError::InvalidMessage)?;
            let length =
                u32::try_from(payload.len()).map_err(|_| HostProcessError::FrameTooLarge)?;
            writer.write_all(&length.to_be_bytes())?;
            writer.write_all(&payload)?;
        }
        writer.flush()?;
        Ok(())
    }

    struct HealthServer {
        path: PathBuf,
        stop: Arc<AtomicBool>,
        worker: Option<JoinHandle<()>>,
    }

    impl HealthServer {
        fn start(path: PathBuf) -> Result<Self, HostProcessError> {
            let listener = UnixListener::bind(&path)?;
            fs::set_permissions(&path, Permissions::from_mode(0o600))?;
            let stop = Arc::new(AtomicBool::new(false));
            let worker_stop = Arc::clone(&stop);
            let worker = thread::Builder::new()
                .name("content-host-health".to_owned())
                .spawn(move || health_loop(listener, &worker_stop));
            let worker = match worker {
                Ok(worker) => worker,
                Err(_) => {
                    remove_owned_socket(&path);
                    return Err(HostProcessError::Io);
                }
            };
            Ok(Self {
                path,
                stop,
                worker: Some(worker),
            })
        }
    }

    impl Drop for HealthServer {
        fn drop(&mut self) {
            self.stop.store(true, Ordering::Release);
            let _ = UnixStream::connect(&self.path);
            if let Some(worker) = self.worker.take() {
                let _ = worker.join();
            }
            remove_owned_socket(&self.path);
        }
    }

    fn health_loop(listener: UnixListener, stop: &AtomicBool) {
        while !stop.load(Ordering::Acquire) {
            let Ok((mut stream, _)) = listener.accept() else {
                break;
            };
            if stop.load(Ordering::Acquire) {
                break;
            }
            let _ = stream.set_read_timeout(Some(HEALTH_IO_TIMEOUT));
            let _ = stream.set_write_timeout(Some(HEALTH_IO_TIMEOUT));
            let mut request = [0; 4];
            if stream.read_exact(&mut request).is_ok() && request == *HEALTH_REQUEST {
                let _ = stream.write_all(HEALTH_REPLY);
            }
        }
    }

    fn remove_owned_socket(path: &Path) {
        if fs::symlink_metadata(path).is_ok_and(|metadata| metadata.file_type().is_socket()) {
            let _ = fs::remove_file(path);
        }
    }

    #[derive(Debug)]
    pub enum HostProcessError {
        InvalidArguments,
        InvalidMessage,
        InvalidState,
        FrameTooLarge,
        TruncatedFrame,
        UnexpectedEof,
        Io,
    }

    impl From<io::Error> for HostProcessError {
        fn from(_: io::Error) -> Self {
            Self::Io
        }
    }

    impl std::fmt::Display for HostProcessError {
        fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str(match self {
                Self::InvalidArguments => "content host arguments rejected",
                Self::InvalidMessage => "content host message rejected",
                Self::InvalidState => "content host state rejected",
                Self::FrameTooLarge => "content host frame exceeds limit",
                Self::TruncatedFrame => "content host frame truncated",
                Self::UnexpectedEof => "content host control pipe closed before shutdown",
                Self::Io => "content host local I/O failed",
            })
        }
    }
}

#[cfg(target_os = "macos")]
fn main() {
    if let Err(error) = macos::run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}

#[cfg(not(target_os = "macos"))]
fn main() {
    eprintln!("crayon-content-host is supported on macOS in CNT-18c");
    std::process::exit(78);
}
