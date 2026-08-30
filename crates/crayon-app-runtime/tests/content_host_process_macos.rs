#![cfg(target_os = "macos")]

use crayon_ipc_schema::{
    decode_content_host_message, encode_content_host_message, ContentHostEngineErrorCode,
    ContentHostMessage, ContentHostMode, ContentHostTerminalStatus, MAX_CONTENT_HOST_FRAME_BYTES,
};
use std::fs::{self, Permissions};
use std::io::{Read, Write};
use std::os::unix::fs::{FileTypeExt, PermissionsExt};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};

static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(1);

struct Harness {
    child: Child,
    input: Option<ChildStdin>,
    output: ChildStdout,
    directory: PathBuf,
    socket: PathBuf,
    finished: bool,
}

impl Harness {
    fn start() -> Self {
        let directory = PathBuf::from("/tmp").join(format!(
            "crayon-content-host-{}-{}",
            std::process::id(),
            NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&directory).unwrap();
        fs::set_permissions(&directory, Permissions::from_mode(0o700)).unwrap();
        let socket = directory.join("health.sock");
        let mut child = Command::new(env!("CARGO_BIN_EXE_crayon-content-host"))
            .arg("--health-socket")
            .arg(&socket)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .unwrap();
        let input = child.stdin.take().unwrap();
        let output = child.stdout.take().unwrap();
        let harness = Self {
            child,
            input: Some(input),
            output,
            directory,
            socket,
            finished: false,
        };
        harness.wait_healthy();
        harness
    }

    fn wait_healthy(&self) {
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            if let Ok(mut stream) = UnixStream::connect(&self.socket) {
                stream.write_all(b"PING").unwrap();
                let mut reply = [0; 4];
                stream.read_exact(&mut reply).unwrap();
                assert_eq!(&reply, b"PONG");
                return;
            }
            assert!(Instant::now() < deadline, "content host health timed out");
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    fn send(&mut self, message: &ContentHostMessage) {
        let payload = encode_content_host_message(message).unwrap();
        let input = self.input.as_mut().unwrap();
        input
            .write_all(&(payload.len() as u32).to_be_bytes())
            .unwrap();
        input.write_all(&payload).unwrap();
        input.flush().unwrap();
    }

    fn receive(&mut self) -> ContentHostMessage {
        let mut header = [0; 4];
        self.output.read_exact(&mut header).unwrap();
        let length = u32::from_be_bytes(header) as usize;
        assert!(length <= MAX_CONTENT_HOST_FRAME_BYTES);
        let mut payload = vec![0; length];
        self.output.read_exact(&mut payload).unwrap();
        decode_content_host_message(&payload).unwrap()
    }

    fn close_input(&mut self) {
        self.input.take();
    }

    fn wait(mut self) -> std::process::ExitStatus {
        self.close_input();
        let status = self.child.wait().unwrap();
        assert!(!self.socket.exists());
        fs::remove_dir(&self.directory).unwrap();
        self.finished = true;
        status
    }
}

impl Drop for Harness {
    fn drop(&mut self) {
        if self.finished {
            return;
        }
        self.close_input();
        let _ = self.child.kill();
        let _ = self.child.wait();
        if fs::symlink_metadata(&self.socket).is_ok_and(|metadata| metadata.file_type().is_socket())
        {
            let _ = fs::remove_file(&self.socket);
        }
        let _ = fs::remove_dir(&self.directory);
    }
}

#[test]
fn cnt_18c_real_process_health_empty_markdown_and_shutdown() {
    let mut harness = Harness::start();
    harness.send(&ContentHostMessage::Begin {
        request_id: "process-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 3,
        generation: 4,
        mode: ContentHostMode::Standard,
        url: "https://example.test/".to_owned(),
        title: "Example".to_owned(),
    });
    harness.send(&ContentHostMessage::Terminal {
        request_id: "process-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 3,
        generation: 4,
        status: ContentHostTerminalStatus::Completed,
        error: ContentHostEngineErrorCode::None,
    });
    assert_eq!(
        harness.receive(),
        ContentHostMessage::MarkdownChunk {
            request_id: "process-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 3,
            generation: 4,
            sequence: 0,
            completed: true,
            markdown: String::new(),
        }
    );
    harness.wait_healthy();
    let stalled_health_client = UnixStream::connect(&harness.socket).unwrap();
    harness.send(&ContentHostMessage::Shutdown);
    assert!(harness.wait().success());
    drop(stalled_health_client);
}

#[test]
fn cnt_18c_eof_and_oversize_frame_fail_closed() {
    let harness = Harness::start();
    assert!(!harness.wait().success());

    let mut harness = Harness::start();
    harness
        .input
        .as_mut()
        .unwrap()
        .write_all(&((MAX_CONTENT_HOST_FRAME_BYTES + 1) as u32).to_be_bytes())
        .unwrap();
    harness.close_input();
    assert!(!harness.wait().success());

    let mut harness = Harness::start();
    harness
        .input
        .as_mut()
        .unwrap()
        .write_all(&1u32.to_be_bytes())
        .unwrap();
    harness.input.as_mut().unwrap().write_all(&[0]).unwrap();
    harness.close_input();
    assert!(!harness.wait().success());
}
