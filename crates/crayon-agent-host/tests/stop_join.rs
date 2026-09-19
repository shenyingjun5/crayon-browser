//! AGT-12Ce stop-path regression: with no client connected, `stop_and_join`
//! must return promptly (the endpoint wake unblocks the parked accept) and
//! the hosted endpoint must unlink the socket file on the serve thread.
//! A client that holds the connection open must instead detach the serve
//! thread at the join timeout without leaking the endpoint teardown.

#![cfg(target_os = "macos")]

use crayon_agent_gateway::grant::ProfileScope;
use crayon_agent_gateway::server::gateway::ToolPort;
use crayon_agent_gateway::server::CancelFlag;
use crayon_agent_gateway::server::ServerExit;
use crayon_agent_gateway::transport::{DecodedFrame, FrameCodec};
use crayon_agent_host::start_macos_uds;
use crayon_domain::{AgentCapability, AgentTarget, CaapError, TabId};
use crayon_ipc_schema::{CaapHello, CaapRequest, CaapWelcome, SchemaVersion};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::{Duration, Instant};

/// A port that never executes: the stop path must not depend on tool work.
struct NeverPort;

impl ToolPort for NeverPort {
    fn resolve_target(&mut self, _target: &AgentTarget) -> Result<TabId, CaapError> {
        Err(CaapError::TargetInvalid)
    }

    fn execute(
        &mut self,
        _tool: &str,
        _request: &CaapRequest,
        _tab: &TabId,
        _cancel: &CancelFlag,
    ) -> Result<String, CaapError> {
        Err(CaapError::InvalidMessage)
    }
}

#[test]
fn stop_without_client_returns_promptly_and_unlinks() {
    let purpose = format!("agent-host-stop-{}", std::process::id());
    let profile = ProfileScope::new("default").expect("profile");
    let (host, socket_path) = start_macos_uds(
        &purpose,
        NeverPort,
        profile,
        600_000,
        SchemaVersion::CURRENT,
        vec![crayon_domain::AgentCapability::PageRead],
        std::sync::Arc::new(|_, _| {}),
    )
    .expect("host starts");
    assert!(std::path::Path::new(&socket_path).exists());

    // No client is connected: the wake must end the parked accept instead
    // of stalling until the join timeout.
    let started = Instant::now();
    let exit = host.stop_and_join(Duration::from_secs(5));
    let elapsed = started.elapsed();
    assert_eq!(exit, Some(ServerExit::Stopped));
    assert!(
        elapsed < Duration::from_secs(2),
        "stop_and_join stalled for {elapsed:?}"
    );

    // The serve thread owns the endpoint; its teardown must unlink.
    assert!(
        !std::path::Path::new(&socket_path).exists(),
        "socket file left behind at {socket_path}"
    );
}

fn frame_bytes<T: serde::ser::Serialize>(value: &T) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(value).unwrap())
}

fn stream_read_chunk(stream: &mut UnixStream) -> Vec<u8> {
    let mut buffer = [0u8; 4096];
    let read = stream.read(buffer.as_mut()).expect("stream read");
    buffer[..read].to_vec()
}

/// A client that finished the handshake and then holds the connection
/// open parks the serve thread in a blocking frame read. The join must
/// give up at its timeout (detached thread) and the detached thread must
/// still run its endpoint teardown once the client disconnects.
#[test]
fn stop_with_connected_client_detaches_at_timeout() {
    let purpose = format!("agent-host-stop-busy-{}", std::process::id());
    let profile = ProfileScope::new("default").expect("profile");
    let (host, socket_path) = start_macos_uds(
        &purpose,
        NeverPort,
        profile,
        600_000,
        SchemaVersion::CURRENT,
        vec![crayon_domain::AgentCapability::PageRead],
        std::sync::Arc::new(|_, _| {}),
    )
    .expect("host starts");

    let mut stream = UnixStream::connect(&socket_path).expect("connect");
    stream
        .set_read_timeout(Some(Duration::from_secs(10)))
        .unwrap();
    let hello = CaapHello::new(
        SchemaVersion::CURRENT,
        "stop-timeout-client",
        vec![AgentCapability::PageRead],
    )
    .expect("hello");
    stream.write_all(&frame_bytes(&hello)).unwrap();
    let mut codec = FrameCodec::new();
    let welcome_payload = loop {
        match codec.feed(&stream_read_chunk(&mut stream)) {
            Ok(DecodedFrame::Complete(payload)) => break payload,
            Ok(_) => continue,
            Err(error) => panic!("welcome failed: {error:?}"),
        }
    };
    let welcome: CaapWelcome = serde_json::from_slice(&welcome_payload).unwrap();
    assert_eq!(welcome.schema(), SchemaVersion::CURRENT);
    // Past the Welcome the serve thread is parked on the next frame read;
    // no stop signal interrupts it.

    let started = Instant::now();
    let exit = host.stop_and_join(Duration::from_millis(200));
    assert_eq!(exit, None, "stuck client must detach at the join timeout");
    assert!(
        started.elapsed() < Duration::from_secs(2),
        "stop_and_join did not respect the join timeout"
    );

    // The detached thread finishes once the client disconnects; its
    // endpoint teardown must still unlink the socket file.
    drop(stream);
    let deadline = Instant::now() + Duration::from_secs(5);
    while std::path::Path::new(&socket_path).exists() && Instant::now() < deadline {
        std::thread::sleep(Duration::from_millis(20));
    }
    assert!(
        !std::path::Path::new(&socket_path).exists(),
        "detached serve thread never unlinked {socket_path}"
    );
}
