//! AGT-05C grant data flow: the connect event surfaces the client and
//! negotiated capabilities; a confirmation-driven grant then lets the
//! SAME connection execute a read tool and receive real data.

#![cfg(target_os = "macos")]

use crayon_agent_gateway::grant::ProfileScope;
use crayon_agent_gateway::server::gateway::ToolPort;
use crayon_agent_gateway::server::CancelFlag;
use crayon_agent_gateway::transport::{DecodedFrame, FrameCodec};
use crayon_agent_host::start_macos_uds;
use crayon_domain::{AgentCapability, AgentTarget, CaapError, TabId};
use crayon_ipc_schema::{CaapChunk, CaapHello, CaapRequest, CaapWelcome, SchemaVersion};
use std::collections::BTreeMap;
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::sync::mpsc::{Receiver, RecvTimeoutError};
use std::sync::{Arc, Mutex};
use std::time::Duration;

/// Port that answers any executed read tool with a fixed title.
struct TitlePort;

impl ToolPort for TitlePort {
    fn resolve_target(&mut self, _target: &AgentTarget) -> Result<TabId, CaapError> {
        TabId::new("tab-1").map_err(|_| CaapError::TargetInvalid)
    }

    fn execute(
        &mut self,
        _tool: &str,
        _request: &CaapRequest,
        _tab: &TabId,
        _cancel: &CancelFlag,
    ) -> Result<String, CaapError> {
        Ok("Confirmed Title".to_owned())
    }
}

#[test]
fn connect_event_grant_then_read_roundtrip() {
    let purpose = format!("agent-host-grant-{}", std::process::id());
    let (connect_tx, connect_rx) = std::sync::mpsc::channel();
    let connect_tx = Mutex::new(connect_tx);
    let on_client: crayon_agent_host::OnClientConnected = Arc::new(move |client, capabilities| {
        let _ = connect_tx
            .lock()
            .unwrap()
            .send((client.to_owned(), capabilities.to_vec()));
    });
    let profile = ProfileScope::new("default").expect("profile");
    let (host, socket_path) = start_macos_uds(
        &purpose,
        TitlePort,
        profile,
        600_000,
        SchemaVersion::CURRENT,
        vec![AgentCapability::PageRead],
        on_client,
    )
    .expect("host starts");

    // Client: handshake and keep the connection open (persistent-client
    // shape, e.g. the MCP server).
    let mut stream = UnixStream::connect(&socket_path).expect("connect");
    stream
        .set_read_timeout(Some(Duration::from_secs(10)))
        .unwrap();
    let hello = CaapHello::new(
        SchemaVersion::CURRENT,
        "grant-flow-client",
        vec![AgentCapability::PageRead],
    )
    .expect("hello");
    stream.write_all(&frame_bytes(&hello)).unwrap();
    let mut codec = FrameCodec::new();
    let welcome: CaapWelcome =
        serde_json::from_slice(&read_frame(&mut stream, &mut codec)).unwrap();
    assert_eq!(welcome.schema(), SchemaVersion::CURRENT);

    // The product learns about the connection with name + capabilities.
    let (client, capabilities) = expect_event(&connect_rx, "connect event must fire after Welcome");
    assert_eq!(client, "grant-flow-client");
    assert!(capabilities.contains(&AgentCapability::PageRead));

    // Before confirmation the read fails closed.
    let request = CaapRequest::new(
        7,
        "page.get_title",
        AgentTarget::ActiveTab,
        now_ms() + 60_000,
        "grant-key",
        BTreeMap::new(),
    )
    .expect("request");
    stream.write_all(&frame_bytes(&request)).unwrap();
    let denied: crayon_ipc_schema::CaapErrorReply =
        serde_json::from_slice(&read_frame(&mut stream, &mut codec)).expect("denial reply");
    assert_eq!(
        denied.error(),
        crayon_domain::CaapError::CapabilityDenied,
        "default deny must hold before the confirmation"
    );

    // The user confirms (AGT-05 outcome): mint the session grant.
    host.issue_grant(AgentCapability::PageRead, None)
        .expect("grant minted for the active client");

    // The SAME persistent connection now reads real data.
    let request = CaapRequest::new(
        8,
        "page.get_title",
        AgentTarget::ActiveTab,
        now_ms() + 60_000,
        // A retry after a denial must carry a fresh idempotency key: the
        // gateway replays the original (failed) task under the same key.
        "grant-key-retry",
        BTreeMap::new(),
    )
    .expect("request");
    stream.write_all(&frame_bytes(&request)).unwrap();
    let payload = read_frame(&mut stream, &mut codec);
    let chunk: CaapChunk = serde_json::from_slice(&payload).unwrap_or_else(|error| {
        panic!(
            "chunk parse failed: {error}; payload={}",
            String::from_utf8_lossy(&payload)
        )
    });
    assert_eq!(chunk.id(), 8);
    assert_eq!(chunk.data(), "Confirmed Title");
    assert!(chunk.is_final());

    drop(stream);
    let _ = host.stop_and_join(Duration::from_secs(5));
}

fn expect_event(
    rx: &Receiver<(String, Vec<AgentCapability>)>,
    message: &str,
) -> (String, Vec<AgentCapability>) {
    match rx.recv_timeout(Duration::from_secs(5)) {
        Ok(event) => event,
        Err(RecvTimeoutError::Timeout) => panic!("{message}"),
        Err(RecvTimeoutError::Disconnected) => panic!("connect sender dropped: {message}"),
    }
}

fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
}

fn frame_bytes<T: serde::ser::Serialize>(value: &T) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(value).unwrap())
}

fn read_frame(stream: &mut UnixStream, codec: &mut FrameCodec) -> Vec<u8> {
    loop {
        match codec.feed(&stream_read_chunk(stream)) {
            Ok(DecodedFrame::Complete(payload)) => return payload,
            Ok(_) => continue,
            Err(error) => panic!("frame failed: {error:?}"),
        }
    }
}

fn stream_read_chunk(stream: &mut UnixStream) -> Vec<u8> {
    let mut buffer = [0u8; 4096];
    let read = stream.read(buffer.as_mut()).expect("stream read");
    buffer[..read].to_vec()
}

/// AGT-05C client binding: a confirmation panel belongs to one client.
/// If that client disconnects and another connects, clicking the stale
/// panel must NOT grant the new (never-confirmed) client.
#[test]
fn stale_panel_confirmation_never_grants_other_client() {
    let purpose = format!("agent-host-toctou-{}", std::process::id());
    let (connect_tx, connect_rx) = std::sync::mpsc::channel();
    let connect_tx = Mutex::new(connect_tx);
    let on_client: crayon_agent_host::OnClientConnected = Arc::new(move |client, capabilities| {
        let _ = connect_tx
            .lock()
            .unwrap()
            .send((client.to_owned(), capabilities.to_vec()));
    });
    let profile = ProfileScope::new("default").expect("profile");
    let (host, socket_path) = start_macos_uds(
        &purpose,
        TitlePort,
        profile,
        600_000,
        SchemaVersion::CURRENT,
        vec![AgentCapability::PageRead],
        on_client,
    )
    .expect("host starts");

    // Client A: the one the (hypothetical) panel was shown for.
    let mut stream_a = UnixStream::connect(&socket_path).expect("connect a");
    stream_a
        .set_read_timeout(Some(Duration::from_secs(10)))
        .unwrap();
    let hello_a = CaapHello::new(
        SchemaVersion::CURRENT,
        "panel-client",
        vec![AgentCapability::PageRead],
    )
    .expect("hello a");
    stream_a.write_all(&frame_bytes(&hello_a)).unwrap();
    let mut codec_a = FrameCodec::new();
    let _: CaapWelcome = serde_json::from_slice(&read_frame(&mut stream_a, &mut codec_a)).unwrap();
    let (client, _) = expect_event(&connect_rx, "event for the panel client");
    assert_eq!(client, "panel-client");

    // A disconnects; a different client B then connects and becomes active.
    drop(stream_a);
    let mut stream_b = UnixStream::connect(&socket_path).expect("connect b");
    stream_b
        .set_read_timeout(Some(Duration::from_secs(10)))
        .unwrap();
    let hello_b = CaapHello::new(
        SchemaVersion::CURRENT,
        "other-client",
        vec![AgentCapability::PageRead],
    )
    .expect("hello b");
    stream_b.write_all(&frame_bytes(&hello_b)).unwrap();
    let mut codec_b = FrameCodec::new();
    let _: CaapWelcome = serde_json::from_slice(&read_frame(&mut stream_b, &mut codec_b)).unwrap();
    let (client, _) = expect_event(&connect_rx, "event for the other client");
    assert_eq!(client, "other-client");

    // Clicking the stale panel (confirmed client = panel-client) must be
    // refused instead of minting for other-client.
    assert!(
        host.issue_grant_for_client("panel-client", AgentCapability::PageRead)
            .is_err(),
        "stale confirmation must not mint a grant"
    );

    // other-client stays denied.
    let request = CaapRequest::new(
        11,
        "page.get_title",
        AgentTarget::ActiveTab,
        now_ms() + 60_000,
        "toctou-key",
        BTreeMap::new(),
    )
    .expect("request");
    stream_b.write_all(&frame_bytes(&request)).unwrap();
    let denied: crayon_ipc_schema::CaapErrorReply =
        serde_json::from_slice(&read_frame(&mut stream_b, &mut codec_b)).expect("denial");
    assert_eq!(denied.error(), crayon_domain::CaapError::CapabilityDenied);

    // Confirming the ACTUAL active client works.
    host.issue_grant_for_client("other-client", AgentCapability::PageRead)
        .expect("grant for the active client");
    let request = CaapRequest::new(
        12,
        "page.get_title",
        AgentTarget::ActiveTab,
        now_ms() + 60_000,
        "toctou-key-2",
        BTreeMap::new(),
    )
    .expect("request");
    stream_b.write_all(&frame_bytes(&request)).unwrap();
    let chunk: CaapChunk =
        serde_json::from_slice(&read_frame(&mut stream_b, &mut codec_b)).unwrap();
    assert_eq!(chunk.data(), "Confirmed Title");
    assert!(chunk.is_final());

    drop(stream_b);
    let _ = host.stop_and_join(Duration::from_secs(5));
}
