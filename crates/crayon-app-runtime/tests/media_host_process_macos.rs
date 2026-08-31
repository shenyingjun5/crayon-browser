#![cfg(target_os = "macos")]

use crayon_ipc_schema::{
    decode_media_host_message, encode_media_host_message, AdContinuity, CastPolicyDecision,
    ExternalClientHandoff, HandoffReason, MediaHostErrorCode, MediaHostMessage, MediaHostPlayback,
    MAX_MEDIA_HOST_FRAME_BYTES,
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
            "crayon-media-host-{}-{}",
            std::process::id(),
            NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&directory).unwrap();
        fs::set_permissions(&directory, Permissions::from_mode(0o700)).unwrap();
        let socket = directory.join("health.sock");
        let mut child = Command::new(env!("CARGO_BIN_EXE_crayon-media-host"))
            .arg("--health-socket")
            .arg(&socket)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .unwrap();
        let harness = Self {
            input: child.stdin.take(),
            output: child.stdout.take().unwrap(),
            child,
            directory,
            socket,
            finished: false,
        };
        harness.wait_healthy();
        harness
    }

    fn wait_healthy(&self) {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if let Ok(mut stream) = UnixStream::connect(&self.socket) {
                stream.write_all(b"PING").unwrap();
                let mut reply = [0; 4];
                stream.read_exact(&mut reply).unwrap();
                assert_eq!(&reply, b"PONG");
                return;
            }
            assert!(Instant::now() < deadline, "media host health timed out");
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    fn send(&mut self, message: &MediaHostMessage) {
        let payload = encode_media_host_message(message).unwrap();
        let input = self.input.as_mut().unwrap();
        input
            .write_all(&(payload.len() as u32).to_be_bytes())
            .unwrap();
        input.write_all(&payload).unwrap();
        input.flush().unwrap();
    }

    fn receive(&mut self) -> MediaHostMessage {
        let mut header = [0; 4];
        self.output.read_exact(&mut header).unwrap();
        let length = u32::from_be_bytes(header) as usize;
        assert!(length <= MAX_MEDIA_HOST_FRAME_BYTES);
        let mut payload = vec![0; length];
        self.output.read_exact(&mut payload).unwrap();
        decode_media_host_message(&payload).unwrap()
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

fn playback() -> MediaHostPlayback {
    MediaHostPlayback {
        position_ms: 1_000,
        duration_ms: None,
        is_live: true,
        ad_continuity: AdContinuity::Unknown,
        current_src: true,
        near_play_event: true,
        audible: true,
        main_frame: true,
        visible_area_px: 10,
    }
}

#[test]
fn real_process_health_url_less_cancel_and_shutdown() {
    let mut harness = Harness::start();
    harness.send(&MediaHostMessage::Navigation {
        request_id: "nav-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 1,
        generation: 1,
    });
    assert!(matches!(
        harness.receive(),
        MediaHostMessage::Ack { request_id } if request_id == "nav-1"
    ));

    harness.send(&MediaHostMessage::DecideUrlLess {
        request_id: "url-less-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 1,
        generation: 1,
        page_url: "https://page.example/watch".to_owned(),
        playback: playback(),
        eme_encrypted: false,
        handoff_available: true,
    });
    assert!(matches!(
        harness.receive(),
        MediaHostMessage::DecisionReply {
            request_id,
            candidate_id: None,
            protocol: None,
            decision: CastPolicyDecision::ExternalClientHandoff(handoff),
        } if request_id == "url-less-1"
            && handoff == ExternalClientHandoff::new(HandoffReason::NoDirectUrl)
    ));

    harness.send(&MediaHostMessage::Cancel {
        request_id: "unknown-request".to_owned(),
    });
    assert!(matches!(
        harness.receive(),
        MediaHostMessage::ErrorReply { request_id, code: MediaHostErrorCode::Cancelled }
            if request_id == "unknown-request"
    ));
    harness.wait_healthy();
    harness.send(&MediaHostMessage::Shutdown);
    assert!(harness.wait().success());
}

#[test]
fn eof_oversize_and_unknown_kind_fail_closed() {
    let harness = Harness::start();
    assert!(!harness.wait().success());

    let mut harness = Harness::start();
    harness
        .input
        .as_mut()
        .unwrap()
        .write_all(&((MAX_MEDIA_HOST_FRAME_BYTES + 1) as u32).to_be_bytes())
        .unwrap();
    harness.close_input();
    assert!(!harness.wait().success());

    let mut harness = Harness::start();
    let mut invalid = encode_media_host_message(&MediaHostMessage::Shutdown).unwrap();
    invalid[6] = 0xff;
    harness
        .input
        .as_mut()
        .unwrap()
        .write_all(&(invalid.len() as u32).to_be_bytes())
        .unwrap();
    harness.input.as_mut().unwrap().write_all(&invalid).unwrap();
    harness.close_input();
    assert!(!harness.wait().success());
}
