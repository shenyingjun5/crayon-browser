#[cfg(target_os = "macos")]
mod macos {
    use crayon_app_runtime::cast_usecase::RelayRevocation;
    use crayon_app_runtime::delivery::CoreSessionBackend;
    use crayon_app_runtime::media_host_cast_runtime::MediaHostCastRuntime;
    use crayon_app_runtime::media_host_runtime::{
        error_reply, message_request_id, MediaHostInterruptAction, MediaHostPendingQueue,
        MediaHostRuntime, MediaHostRuntimeError, PreparedMediaHostDecision,
    };
    use crayon_cast_adapter::{
        CapabilityCacheConfig, CastFacade, ReceiverCapabilityCache, SenderCastFacade,
        SenderCastFacadeConfig,
    };
    use crayon_ipc_schema::{
        decode_media_host_message, encode_media_host_message, MediaHostMessage,
        MAX_MEDIA_HOST_FRAME_BYTES,
    };
    use crayon_media_probe::http::{ProbeHttpClient, ProbeHttpConfig};
    use crayon_media_probe::MediaInspector;
    use crayon_relay::runtime::{RelayRuntime, RelayRuntimeConfig};
    use std::fs::{self, Permissions};
    use std::io::{self, Read, Write};
    use std::os::unix::fs::{FileTypeExt, PermissionsExt};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::thread::{self, JoinHandle};
    use std::time::Duration;
    use tokio::sync::mpsc;

    const HEALTH_REQUEST: &[u8; 4] = b"PING";
    const HEALTH_REPLY: &[u8; 4] = b"PONG";
    const HEALTH_IO_TIMEOUT: Duration = Duration::from_millis(250);

    enum ReaderEvent {
        Message(MediaHostMessage),
        Eof,
        Failed,
    }

    pub(super) enum DecisionOutcome {
        Completed(Result<MediaHostMessage, MediaHostRuntimeError>),
        Cancelled,
        Shutdown,
        InputClosed,
    }

    pub fn run() -> Result<(), HostProcessError> {
        let socket_path = arguments()?;
        let health = HealthServer::start(socket_path)?;
        let (sender, receiver) =
            mpsc::channel(crayon_app_runtime::media_host_runtime::MAX_MEDIA_HOST_PENDING_MESSAGES);
        let _reader = spawn_reader(sender)?;
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .enable_all()
            .build()
            .map_err(|_| HostProcessError::Runtime)?;
        let result = runtime.block_on(run_loop(receiver));
        drop(health);
        result
    }

    async fn run_loop(mut receiver: mpsc::Receiver<ReaderEvent>) -> Result<(), HostProcessError> {
        let services = CastServices::start().await?;
        let result = run_loop_with_services(&mut receiver, &services).await;
        services.stop().await;
        result
    }

    async fn run_loop_with_services(
        receiver: &mut mpsc::Receiver<ReaderEvent>,
        services: &CastServices,
    ) -> Result<(), HostProcessError> {
        let stdout = io::stdout();
        let mut output = stdout.lock();
        let mut host = MediaHostRuntime::with_cast(
            MediaInspector::new(ProbeHttpClient::new(ProbeHttpConfig::default())),
            Arc::clone(&services.runtime),
        );
        let mut pending = MediaHostPendingQueue::default();
        loop {
            let event = match pending.pop_front() {
                Some(message) => ReaderEvent::Message(message),
                None => receiver.recv().await.ok_or(HostProcessError::Reader)?,
            };
            let message = match event {
                ReaderEvent::Message(message) => message,
                ReaderEvent::Eof => return Err(HostProcessError::UnexpectedEof),
                ReaderEvent::Failed => return Err(HostProcessError::InvalidFrame),
            };
            if matches!(message, MediaHostMessage::Shutdown) {
                host.shutdown_cast()
                    .await
                    .map_err(|_| HostProcessError::InvalidState)?;
                host.handle_immediate(message)
                    .map_err(|_| HostProcessError::InvalidState)?;
                return Ok(());
            }
            if matches!(
                message,
                MediaHostMessage::Decide { .. } | MediaHostMessage::DecideUrlLess { .. }
            ) {
                let request_id = message_request_id(&message)
                    .ok_or(HostProcessError::InvalidMessage)?
                    .to_owned();
                let prepared = match host.prepare_decision(message) {
                    Ok(prepared) => prepared,
                    Err(error) => {
                        write_message(&mut output, &error_reply(request_id, error))?;
                        continue;
                    }
                };
                match drive_decision(&host, prepared, receiver, &mut pending, &mut output).await? {
                    DecisionOutcome::Completed(result) => {
                        write_runtime_result(&mut output, request_id, result)?;
                    }
                    DecisionOutcome::Cancelled => {
                        write_message(
                            &mut output,
                            &error_reply(request_id, MediaHostRuntimeError::Cancelled),
                        )?;
                    }
                    DecisionOutcome::Shutdown => {
                        host.handle_immediate(MediaHostMessage::Shutdown)
                            .map_err(|_| HostProcessError::InvalidState)?;
                        return Ok(());
                    }
                    DecisionOutcome::InputClosed => {
                        return Err(HostProcessError::UnexpectedEof);
                    }
                }
                continue;
            }
            if matches!(
                message,
                MediaHostMessage::Discovery { .. }
                    | MediaHostMessage::ListDevices { .. }
                    | MediaHostMessage::StartCast { .. }
                    | MediaHostMessage::StopCast { .. }
                    | MediaHostMessage::PollSessionEvents { .. }
            ) {
                let request_id = message_request_id(&message)
                    .ok_or(HostProcessError::InvalidMessage)?
                    .to_owned();
                let prepared = match host.prepare_cast_command(message) {
                    Ok(prepared) => prepared,
                    Err(error) => {
                        write_message(&mut output, &error_reply(request_id, error))?;
                        continue;
                    }
                };
                let result = host.execute_cast_command(prepared).await;
                write_runtime_result(&mut output, request_id, result)?;
                continue;
            }
            let request_id = message_request_id(&message).map(str::to_owned);
            match host.handle_immediate(message) {
                Ok(Some(reply)) => write_message(&mut output, &reply)?,
                Ok(None) => return Ok(()),
                Err(error) => {
                    let request_id = request_id.ok_or(HostProcessError::InvalidMessage)?;
                    write_message(&mut output, &error_reply(request_id, error))?;
                }
            }
        }
    }

    struct CastServices {
        runtime: Arc<MediaHostCastRuntime>,
        facade: Arc<SenderCastFacade>,
        relay: Arc<RelayRuntime>,
    }

    impl CastServices {
        async fn start() -> Result<Self, HostProcessError> {
            let relay = RelayRuntime::start(RelayRuntimeConfig {
                control_secret: process_secret()?,
                ..RelayRuntimeConfig::default()
            })
            .await
            .map_err(|_| HostProcessError::Runtime)?;
            let facade = Arc::new(SenderCastFacade::new(SenderCastFacadeConfig::default()));
            let facade_port: Arc<dyn CastFacade> = facade.clone();
            let capabilities = Arc::new(ReceiverCapabilityCache::new(
                Arc::clone(&facade_port),
                CapabilityCacheConfig::default(),
            ));
            let backend = Box::new(CoreSessionBackend::new(
                relay.core().clone(),
                relay.media_base_url(),
            ));
            let revocation: Arc<dyn RelayRevocation> = relay.clone();
            let runtime = Arc::new(MediaHostCastRuntime::new(
                facade_port,
                capabilities,
                backend,
                revocation,
            ));
            Ok(Self {
                runtime,
                facade,
                relay,
            })
        }

        async fn stop(&self) {
            self.runtime.on_app_exit();
            self.facade.shutdown();
            self.relay.stop().await;
        }
    }

    fn process_secret() -> Result<String, HostProcessError> {
        let mut bytes = [0u8; 32];
        getrandom::fill(&mut bytes).map_err(|_| HostProcessError::Runtime)?;
        let mut value = String::with_capacity(bytes.len() * 2);
        const HEX: &[u8; 16] = b"0123456789abcdef";
        for byte in bytes {
            value.push(HEX[(byte >> 4) as usize] as char);
            value.push(HEX[(byte & 0x0f) as usize] as char);
        }
        Ok(value)
    }

    async fn drive_decision(
        host: &MediaHostRuntime,
        prepared: PreparedMediaHostDecision,
        receiver: &mut mpsc::Receiver<ReaderEvent>,
        pending: &mut MediaHostPendingQueue,
        output: &mut impl Write,
    ) -> Result<DecisionOutcome, HostProcessError> {
        let active_request = prepared.request_id().to_owned();
        let decision = host.execute_decision(prepared);
        tokio::pin!(decision);
        loop {
            tokio::select! {
                result = &mut decision => return Ok(DecisionOutcome::Completed(result)),
                event = receiver.recv() => {
                    let Some(event) = event else {
                        return Ok(DecisionOutcome::InputClosed);
                    };
                    let message = match event {
                        ReaderEvent::Message(message) => message,
                        ReaderEvent::Eof => return Ok(DecisionOutcome::InputClosed),
                        ReaderEvent::Failed => return Err(HostProcessError::InvalidFrame),
                    };
                    let (action, reply) = pending
                        .accept_during_decision(&active_request, message)
                        .map_err(|_| HostProcessError::InvalidState)?;
                    if let Some(reply) = reply {
                        write_message(output, &reply)?;
                    }
                    match action {
                        MediaHostInterruptAction::Continue => {}
                        MediaHostInterruptAction::Cancel => return Ok(DecisionOutcome::Cancelled),
                        MediaHostInterruptAction::Shutdown => return Ok(DecisionOutcome::Shutdown),
                    }
                }
            }
        }
    }

    fn write_runtime_result(
        output: &mut impl Write,
        request_id: String,
        result: Result<MediaHostMessage, MediaHostRuntimeError>,
    ) -> Result<(), HostProcessError> {
        match result {
            Ok(reply) => write_message(output, &reply),
            Err(error) => write_message(output, &error_reply(request_id, error)),
        }
    }

    fn write_message(
        output: &mut impl Write,
        message: &MediaHostMessage,
    ) -> Result<(), HostProcessError> {
        let payload =
            encode_media_host_message(message).map_err(|_| HostProcessError::InvalidMessage)?;
        let length = u32::try_from(payload.len()).map_err(|_| HostProcessError::FrameTooLarge)?;
        output.write_all(&length.to_be_bytes())?;
        output.write_all(&payload)?;
        output.flush()?;
        Ok(())
    }

    fn spawn_reader(sender: mpsc::Sender<ReaderEvent>) -> Result<JoinHandle<()>, HostProcessError> {
        thread::Builder::new()
            .name("media-host-reader".to_owned())
            .spawn(move || {
                let stdin = io::stdin();
                let mut input = stdin.lock();
                loop {
                    match read_frame(&mut input) {
                        Ok(Some(payload)) => match decode_media_host_message(&payload) {
                            Ok(message) => {
                                if sender.blocking_send(ReaderEvent::Message(message)).is_err() {
                                    return;
                                }
                            }
                            Err(_) => {
                                let _ = sender.blocking_send(ReaderEvent::Failed);
                                return;
                            }
                        },
                        Ok(None) => {
                            let _ = sender.blocking_send(ReaderEvent::Eof);
                            return;
                        }
                        Err(_) => {
                            let _ = sender.blocking_send(ReaderEvent::Failed);
                            return;
                        }
                    }
                }
            })
            .map_err(|_| HostProcessError::Reader)
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
        if length > MAX_MEDIA_HOST_FRAME_BYTES {
            return Err(HostProcessError::FrameTooLarge);
        }
        let mut payload = vec![0; length];
        reader
            .read_exact(&mut payload)
            .map_err(|_| HostProcessError::TruncatedFrame)?;
        Ok(Some(payload))
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
                .name("media-host-health".to_owned())
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
        InvalidFrame,
        FrameTooLarge,
        TruncatedFrame,
        UnexpectedEof,
        Reader,
        Runtime,
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
                Self::InvalidArguments => "media host arguments rejected",
                Self::InvalidMessage => "media host message rejected",
                Self::InvalidState => "media host state rejected",
                Self::InvalidFrame => "media host frame rejected",
                Self::FrameTooLarge => "media host frame exceeds limit",
                Self::TruncatedFrame => "media host frame truncated",
                Self::UnexpectedEof => "media host control pipe closed before shutdown",
                Self::Reader => "media host reader failed",
                Self::Runtime => "media host runtime failed",
                Self::Io => "media host local I/O failed",
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
    eprintln!("crayon-media-host is supported on macOS in PLT-M05b2b1");
    std::process::exit(78);
}
