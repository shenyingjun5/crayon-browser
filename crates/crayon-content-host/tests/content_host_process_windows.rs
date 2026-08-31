#![cfg(target_os = "windows")]

use crayon_ipc_schema::{
    decode_content_host_message, encode_content_host_message, ContentHostEngineErrorCode,
    ContentHostMessage, ContentHostMode, ContentHostTerminalStatus, MAX_CONTENT_HOST_FRAME_BYTES,
};
use crayon_platform_windows::local_agent_ipc::WindowsAgentIpcClient;
use std::io::{Read, Write};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};

static NEXT_PIPE: AtomicU64 = AtomicU64::new(1);

struct Harness {
    child: Child,
    input: Option<ChildStdin>,
    output: ChildStdout,
    path: Vec<u16>,
    finished: bool,
}

impl Harness {
    fn start() -> Self {
        let purpose = format!(
            "content-health-{}-{}",
            std::process::id(),
            NEXT_PIPE.fetch_add(1, Ordering::Relaxed)
        );
        let mut path: Vec<u16> = format!(r"\\.\pipe\crayon-agent-{purpose}")
            .encode_utf16()
            .collect();
        path.push(0);
        let mut child = Command::new(env!("CARGO_BIN_EXE_crayon-content-host"))
            .arg("--health-pipe")
            .arg(&purpose)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .expect("start content host");
        let input = child.stdin.take().expect("content host stdin");
        let output = child.stdout.take().expect("content host stdout");
        let harness = Self {
            child,
            input: Some(input),
            output,
            path,
            finished: false,
        };
        harness.wait_healthy();
        harness
    }

    fn wait_healthy(&self) {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if let Ok(mut stream) = WindowsAgentIpcClient::connect(&self.path) {
                stream.write_all(b"PING").expect("health request");
                let mut reply = [0; 4];
                stream.read_exact(&mut reply).expect("health reply");
                assert_eq!(&reply, b"PONG");
                return;
            }
            assert!(Instant::now() < deadline, "content host health timed out");
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    fn send(&mut self, message: &ContentHostMessage) {
        let payload = encode_content_host_message(message).expect("encode message");
        let input = self.input.as_mut().expect("live input");
        input
            .write_all(&(payload.len() as u32).to_be_bytes())
            .expect("write frame length");
        input.write_all(&payload).expect("write frame payload");
        input.flush().expect("flush frame");
    }

    fn receive(&mut self) -> ContentHostMessage {
        let mut header = [0; 4];
        self.output
            .read_exact(&mut header)
            .expect("read frame length");
        let length = u32::from_be_bytes(header) as usize;
        assert!(length <= MAX_CONTENT_HOST_FRAME_BYTES);
        let mut payload = vec![0; length];
        self.output
            .read_exact(&mut payload)
            .expect("read frame payload");
        decode_content_host_message(&payload).expect("decode message")
    }

    fn close_input(&mut self) {
        self.input.take();
    }

    fn wait(mut self) -> std::process::ExitStatus {
        self.close_input();
        let status = self.child.wait().expect("wait content host");
        assert!(WindowsAgentIpcClient::connect(&self.path).is_err());
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
    }
}

#[test]
fn cnt_20w1_real_process_health_markdown_and_shutdown() {
    let mut harness = Harness::start();
    harness.send(&ContentHostMessage::Begin {
        request_id: "process-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 3,
        generation: 3,
        mode: ContentHostMode::Standard,
        url: "https://example.test/".to_owned(),
        title: "Example".to_owned(),
    });
    harness.send(&ContentHostMessage::FactBatch {
        request_id: "process-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 3,
        generation: 3,
        sequence: 0,
        facts: vec![crayon_ipc_schema::ContentHostFact {
            kind: crayon_ipc_schema::ContentHostFactKind::Paragraph,
            text: "Windows body".to_owned(),
            url: None,
            language: None,
            level: 0,
            depth: 0,
            ordered: false,
            ordinal: None,
            table_columns: 0,
            table_cells: vec![],
        }],
    });
    harness.send(&ContentHostMessage::Terminal {
        request_id: "process-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 3,
        generation: 3,
        status: ContentHostTerminalStatus::Completed,
        error: ContentHostEngineErrorCode::None,
    });
    assert_eq!(
        harness.receive(),
        ContentHostMessage::MarkdownChunk {
            request_id: "process-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 3,
            generation: 3,
            sequence: 0,
            completed: true,
            markdown: "Windows body\n".to_owned(),
        }
    );
    harness.wait_healthy();
    harness.send(&ContentHostMessage::Shutdown);
    assert!(harness.wait().success());
}

#[test]
fn cnt_20w1_eof_and_oversize_frame_fail_closed() {
    let harness = Harness::start();
    assert!(!harness.wait().success());

    let mut harness = Harness::start();
    harness
        .input
        .as_mut()
        .expect("live input")
        .write_all(&((MAX_CONTENT_HOST_FRAME_BYTES + 1) as u32).to_be_bytes())
        .expect("write oversize header");
    harness.close_input();
    assert!(!harness.wait().success());
}
