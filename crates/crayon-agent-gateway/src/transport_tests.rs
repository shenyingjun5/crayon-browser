//! AGT-12A transport guard tests: framing matrix, single-client
//! admission, rate limiting, replay rejection, strikes and stop.

use super::*;
use crayon_domain::{AgentCapability, AgentTarget};
use crayon_platform_api::local_agent_ipc::{
    LocalAgentIpcConnection, LocalAgentIpcEndpoint, LocalAgentIpcError, PeerIdentity,
};
use std::collections::BTreeMap;
use std::io::{Cursor, Read, Write};
use std::num::NonZeroU16;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

fn header(len: usize) -> Vec<u8> {
    (len as u32).to_be_bytes().to_vec()
}

fn json_frame<T: serde::Serialize>(value: &T) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(value).unwrap())
}

fn decode_output<T: serde::de::DeserializeOwned>(bytes: &[u8]) -> T {
    let mut codec = FrameCodec::new();
    let DecodedFrame::Complete(payload) = codec.feed(bytes).unwrap() else {
        panic!("expected complete output frame");
    };
    serde_json::from_slice(&payload).unwrap()
}

struct MemoryConnection {
    input: Cursor<Vec<u8>>,
    output: Arc<Mutex<Vec<u8>>>,
    closed: Arc<AtomicBool>,
    reads: Arc<AtomicUsize>,
    peer: PeerIdentity,
    max_read: usize,
    fail_write: bool,
}

impl MemoryConnection {
    fn new(input: Vec<u8>, peer: PeerIdentity, max_read: usize) -> (Self, MemoryProbe) {
        let output = Arc::new(Mutex::new(Vec::new()));
        let closed = Arc::new(AtomicBool::new(false));
        let reads = Arc::new(AtomicUsize::new(0));
        (
            Self {
                input: Cursor::new(input),
                output: Arc::clone(&output),
                closed: Arc::clone(&closed),
                reads: Arc::clone(&reads),
                peer,
                max_read,
                fail_write: false,
            },
            MemoryProbe {
                output,
                closed,
                reads,
            },
        )
    }
}

struct MemoryProbe {
    output: Arc<Mutex<Vec<u8>>>,
    closed: Arc<AtomicBool>,
    reads: Arc<AtomicUsize>,
}

impl Read for MemoryConnection {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        self.reads.fetch_add(1, Ordering::Relaxed);
        let limit = buffer.len().min(self.max_read.max(1));
        self.input.read(&mut buffer[..limit])
    }
}

impl Write for MemoryConnection {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        if self.fail_write {
            return Err(std::io::Error::other("injected write failure"));
        }
        self.output.lock().unwrap().extend_from_slice(buffer);
        Ok(buffer.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl LocalAgentIpcConnection for MemoryConnection {
    fn peer_identity(&self) -> PeerIdentity {
        self.peer
    }

    fn close(&mut self) -> Result<(), LocalAgentIpcError> {
        self.closed.store(true, Ordering::Relaxed);
        Ok(())
    }
}

struct FakeEndpoint {
    stream: Mutex<Option<MemoryConnection>>,
    running: AtomicBool,
}

impl LocalAgentIpcEndpoint for FakeEndpoint {
    fn start(&mut self) -> Result<(), LocalAgentIpcError> {
        self.running.store(true, Ordering::Relaxed);
        Ok(())
    }

    fn accept(&self) -> Result<Box<dyn LocalAgentIpcConnection + '_>, LocalAgentIpcError> {
        self.stream
            .lock()
            .unwrap()
            .take()
            .map(|stream| Box::new(stream) as Box<dyn LocalAgentIpcConnection>)
            .ok_or(LocalAgentIpcError::HandshakeFailed)
    }

    fn stop(&mut self) -> Result<(), LocalAgentIpcError> {
        self.running.store(false, Ordering::Relaxed);
        Ok(())
    }

    fn is_running(&self) -> bool {
        self.running.load(Ordering::Relaxed)
    }
}

fn hello(client: &str, capabilities: Vec<AgentCapability>) -> CaapHello {
    CaapHello::new(SchemaVersion::CURRENT, client, capabilities).unwrap()
}

fn request(id: u64) -> CaapRequest {
    CaapRequest::new(
        id,
        "page.get_title",
        AgentTarget::ActiveTab,
        1000,
        &format!("idem-{id}"),
        BTreeMap::new(),
    )
    .unwrap()
}

#[test]
fn frame_codec_round_trip() {
    let frame = FrameCodec::encode(b"hello");
    let mut codec = FrameCodec::new();
    assert_eq!(
        codec.feed(&frame).unwrap(),
        DecodedFrame::Complete(b"hello".to_vec())
    );
    assert_eq!(codec.take().unwrap(), DecodedFrame::Incomplete);
}

#[test]
fn frame_codec_partial_chunks() {
    let frame = FrameCodec::encode(&[7u8; 100]);
    let mut codec = FrameCodec::new();
    assert_eq!(codec.feed(&frame[..3]).unwrap(), DecodedFrame::Incomplete);
    assert_eq!(codec.feed(&frame[3..50]).unwrap(), DecodedFrame::Incomplete);
    assert_eq!(
        codec.feed(&frame[50..]).unwrap(),
        DecodedFrame::Complete(vec![7u8; 100])
    );
    assert_eq!(codec.pending_bytes(), 0);
}

#[test]
fn frame_codec_oversize_and_back_to_back() {
    let mut codec = FrameCodec::new();
    let mut oversize = header(MAX_FRAME_BYTES + 1);
    oversize.extend_from_slice(&[0u8; 16]);
    assert_eq!(
        codec.feed(&oversize).unwrap(),
        DecodedFrame::Oversize {
            declared: (MAX_FRAME_BYTES + 1) as u32
        }
    );
    // Back-to-back legal frames after one complete decode.
    let mut stream = FrameCodec::encode(b"a");
    stream.extend(FrameCodec::encode(b"bb"));
    let mut codec2 = FrameCodec::new();
    assert_eq!(
        codec2.feed(&stream).unwrap(),
        DecodedFrame::Complete(b"a".to_vec())
    );
    assert_eq!(
        codec2.take().unwrap(),
        DecodedFrame::Complete(b"bb".to_vec())
    );
    assert_eq!(codec2.take().unwrap(), DecodedFrame::Incomplete);
}

#[test]
fn frame_codec_max_legal_size_accepted() {
    let frame = FrameCodec::encode(&vec![1u8; MAX_FRAME_BYTES]);
    let mut codec = FrameCodec::new();
    assert_eq!(
        codec.feed(&frame).unwrap(),
        DecodedFrame::Complete(vec![1u8; MAX_FRAME_BYTES])
    );
}

#[test]
fn frame_codec_buffer_bound_fails_closed() {
    let mut codec = FrameCodec::new();
    // Legal incomplete frames can hold at most MAX+3 pending bytes, so
    // the fail-closed bound is reachable only through a single hostile
    // oversized chunk — which must be rejected without buffering.
    let huge = vec![0u8; MAX_FRAME_BYTES * 2 + 1];
    assert_eq!(codec.feed(&huge), Err(TransportError::FrameMalformed));
    assert_eq!(codec.pending_bytes(), 0);
}

#[test]
fn single_client_admission_matrix() {
    let mut guard = TransportGuard::new();
    assert_eq!(guard.bind_client("cli-dev"), Ok(()));
    assert_eq!(guard.bind_client("cli-dev"), Ok(())); // idempotent rebind
    assert_eq!(
        guard.bind_client("mcp-dev"),
        Err(TransportError::ClientBound)
    );
    guard.disconnect();
    assert_eq!(guard.bound_client(), None);
    assert_eq!(guard.bind_client("mcp-dev"), Ok(()));
}

#[test]
fn rate_limit_burst_and_refill() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    // Drain the burst inside one window (no refill elapses).
    for _ in 0..RATE_BURST {
        assert_eq!(guard.admit_rate(0), Ok(()));
    }
    assert_eq!(guard.admit_rate(0), Err(TransportError::RateLimited));
    assert_eq!(
        guard.admit_rate(RATE_INTERVAL_MS - 1),
        Err(TransportError::RateLimited)
    );
    // Refill after one interval passes.
    assert_eq!(
        guard.admit_rate((RATE_BURST as u64) * RATE_INTERVAL_MS + RATE_INTERVAL_MS),
        Ok(())
    );
    // Unbound client is stopped.
    guard.disconnect();
    assert_eq!(guard.admit_rate(0), Err(TransportError::Stopped));
}

#[test]
fn request_id_replay_rejected_with_bounded_window() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    assert_eq!(guard.admit_request_id(1), Ok(()));
    assert_eq!(guard.admit_request_id(1), Err(TransportError::Replayed));
    for id in 2..(MAX_SEEN_IDS as u64 + 10) {
        assert_eq!(guard.admit_request_id(id), Ok(()));
    }
    // The window slid: id 1 was evicted and is accepted again (bounded
    // memory takes precedence over unbounded replay memory; session
    // idempotency provides the stronger semantic dedupe).
    assert_eq!(guard.admit_request_id(1), Ok(()));
    guard.disconnect();
    assert_eq!(guard.admit_request_id(9), Err(TransportError::Stopped));
}

#[test]
fn strikes_drop_client_at_threshold() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    for _ in 0..MAX_STRIKES - 1 {
        assert_eq!(guard.strike(), Ok(()));
    }
    assert_eq!(guard.strike(), Err(TransportError::StrikesExceeded));
    assert_eq!(guard.bound_client(), None);
    assert_eq!(guard.strikes(), 0);
    // State fully reset: a new client binds cleanly.
    assert_eq!(guard.bind_client("next"), Ok(()));
}

#[test]
fn stop_is_idempotent_and_releases() {
    let mut guard = TransportGuard::new();
    guard.bind_client("cli-dev").unwrap();
    guard.stop();
    guard.stop();
    assert_eq!(guard.bound_client(), None);
    assert_eq!(guard.bind_client("other"), Ok(()));
}

#[test]
fn error_display_and_caap_mapping_golden() {
    use crayon_domain::CaapError;
    let cases: &[(TransportError, &str, CaapError)] = &[
        (
            TransportError::FrameTooLarge,
            "frame exceeds size limit",
            CaapError::InvalidMessage,
        ),
        (
            TransportError::FrameMalformed,
            "frame malformed",
            CaapError::InvalidMessage,
        ),
        (
            TransportError::ClientBound,
            "another client already holds the transport",
            CaapError::Unauthorized,
        ),
        (
            TransportError::RateLimited,
            "rate limit exceeded",
            CaapError::QueueFull,
        ),
        (
            TransportError::Replayed,
            "request id replayed",
            CaapError::InvalidMessage,
        ),
        (
            TransportError::StrikesExceeded,
            "too many protocol violations",
            CaapError::Unauthorized,
        ),
        (
            TransportError::Stopped,
            "transport stopped",
            CaapError::Unauthorized,
        ),
    ];
    for (error, message, caap) in cases {
        assert_eq!(error.to_string(), *message);
        assert_eq!(error.to_caap_error(), *caap);
    }
}

/// Deterministic pseudo-random sequence (LCG): a hostile client mix of
/// oversize, malformed and replay frames must never panic, never exceed
/// the strike bound without a drop, and always keep bounded state.
#[test]
fn lcg_hostile_stream_invariants() {
    let mut state: u64 = 0x9E37_79B9_7F4A_7C15;
    let mut next = move || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };

    let mut guard = TransportGuard::new();
    guard.bind_client("hostile").unwrap();
    let mut codec = FrameCodec::new();
    let mut clock = 0_u64;
    let mut next_id = 1_u64;
    for step in 0..3_000_u64 {
        clock += 3;
        let choice = next() % 10;
        if choice < 4 {
            // Well-formed frame with a fresh request id.
            let payload = vec![step as u8; (next() % 64) as usize];
            let frame = FrameCodec::encode(&payload);
            let decoded = codec.feed(&frame).unwrap();
            assert!(matches!(decoded, DecodedFrame::Complete(_)));
            if guard.admit_rate(clock).is_ok() {
                assert_eq!(guard.admit_request_id(next_id), Ok(()));
                next_id += 1;
            }
        } else if choice < 6 {
            // Oversize declaration: a strike, never a panic.
            let _ = codec.feed(&header(MAX_FRAME_BYTES + 1 + (next() % 4) as usize));
            if guard.strike().is_err() {
                guard.bind_client("hostile").unwrap();
            }
        } else if choice < 8 {
            // Replay of an old id.
            if next_id > 2 && guard.admit_rate(clock).is_ok() {
                let _ = guard.admit_request_id(next_id - 1);
            }
        } else {
            // Idle tick lets the bucket refill.
            clock += RATE_INTERVAL_MS;
        }
        assert!(guard.strikes() < MAX_STRIKES || guard.bound_client().is_none());
        assert!(codec.pending_bytes() <= MAX_FRAME_BYTES * 2);
    }
}

#[test]
fn connection_fragmented_handshake_negotiates_and_writes_welcome() {
    let input = json_frame(&hello(
        "cli-dev",
        vec![AgentCapability::PageRead, AgentCapability::Navigation],
    ));
    let (stream, probe) = MemoryConnection::new(input, PeerIdentity::new(true, true), 3);
    let mut connection = CaapConnection::from_stream(
        Box::new(stream),
        SchemaVersion::CURRENT,
        vec![AgentCapability::PageRead],
    );
    let welcome = connection.handshake(0).unwrap();
    assert_eq!(welcome.schema(), SchemaVersion::CURRENT);
    assert_eq!(welcome.capabilities(), &[AgentCapability::PageRead]);
    let output = probe.output.lock().unwrap().clone();
    assert_eq!(decode_output::<CaapWelcome>(&output), welcome);
    connection.stop().unwrap();
    connection.stop().unwrap();
    assert!(probe.closed.load(Ordering::Relaxed));
}

#[test]
fn connection_requires_handshake_then_accepts_request_and_cancel() {
    let mut input = json_frame(&hello("cli-dev", vec![AgentCapability::PageRead]));
    input.extend(json_frame(&request(7)));
    input.extend(json_frame(&CaapCancel::new(7)));
    input.extend(json_frame(&request(7)));
    let (stream, _) = MemoryConnection::new(input, PeerIdentity::new(true, true), usize::MAX);
    let mut connection = CaapConnection::from_stream(
        Box::new(stream),
        SchemaVersion::CURRENT,
        vec![AgentCapability::PageRead],
    );
    assert_eq!(
        connection.receive(0),
        Err(ConnectionError::HandshakeRequired)
    );
    connection.handshake(0).unwrap();
    assert!(matches!(
        connection.receive(1),
        Ok(InboundMessage::Request(message)) if message.id() == 7
    ));
    assert_eq!(
        connection.receive(2),
        Ok(InboundMessage::Cancel(CaapCancel::new(7)))
    );
    assert_eq!(
        connection.receive(3),
        Err(ConnectionError::Transport(TransportError::Replayed))
    );
    assert_eq!(
        connection.handshake(4),
        Err(ConnectionError::HandshakeRepeated)
    );
}

#[test]
fn connection_version_error_is_framed_and_payload_free() {
    let unsupported = SchemaVersion::new(NonZeroU16::new(2).unwrap());
    let input = json_frame(&CaapHello::new(unsupported, "cli-dev", vec![]).unwrap());
    let (stream, probe) = MemoryConnection::new(input, PeerIdentity::new(true, true), 64);
    let mut connection =
        CaapConnection::from_stream(Box::new(stream), SchemaVersion::CURRENT, Vec::new());
    assert_eq!(
        connection.handshake(0),
        Err(ConnectionError::VersionUnsupported)
    );
    let output = probe.output.lock().unwrap().clone();
    assert_eq!(
        decode_output::<CaapErrorReply>(&output),
        CaapErrorReply::new(0, CaapError::VersionUnsupported)
    );
    assert!(!String::from_utf8_lossy(&output).contains("cli-dev"));
    assert!(probe.closed.load(Ordering::Relaxed));
}

#[test]
fn endpoint_peer_gate_runs_before_first_handshake_read() {
    let input = json_frame(&hello("hostile", vec![]));
    let (stream, probe) = MemoryConnection::new(input, PeerIdentity::new(false, true), 64);
    let endpoint = FakeEndpoint {
        stream: Mutex::new(Some(stream)),
        running: AtomicBool::new(true),
    };
    assert!(matches!(
        CaapConnection::accept(&endpoint, SchemaVersion::CURRENT, Vec::new()),
        Err(ConnectionError::Endpoint(LocalAgentIpcError::PeerRejected))
    ));
    assert_eq!(probe.reads.load(Ordering::Relaxed), 0);
}

#[test]
fn connection_oversize_and_eof_close_without_unbounded_reads() {
    let (stream, probe) = MemoryConnection::new(
        header(MAX_FRAME_BYTES + 1),
        PeerIdentity::new(true, true),
        CONNECTION_READ_BYTES,
    );
    let mut connection =
        CaapConnection::from_stream(Box::new(stream), SchemaVersion::CURRENT, Vec::new());
    assert_eq!(
        connection.handshake(0),
        Err(ConnectionError::Transport(TransportError::FrameTooLarge))
    );
    assert!(probe.closed.load(Ordering::Relaxed));
    assert_eq!(probe.reads.load(Ordering::Relaxed), 1);

    let (empty, empty_probe) = MemoryConnection::new(Vec::new(), PeerIdentity::new(true, true), 64);
    let mut eof = CaapConnection::from_stream(Box::new(empty), SchemaVersion::CURRENT, Vec::new());
    assert_eq!(eof.handshake(0), Err(ConnectionError::Closed));
    assert!(empty_probe.closed.load(Ordering::Relaxed));

    let input = json_frame(&hello("cli-dev", vec![]));
    let (mut failing, failing_probe) =
        MemoryConnection::new(input, PeerIdentity::new(true, true), 64);
    failing.fail_write = true;
    let mut write_failure =
        CaapConnection::from_stream(Box::new(failing), SchemaVersion::CURRENT, Vec::new());
    assert_eq!(write_failure.handshake(0), Err(ConnectionError::Io));
    assert!(failing_probe.closed.load(Ordering::Relaxed));
}

#[cfg(windows)]
#[test]
fn windows_named_pipe_runs_real_hello_request_cancel_flow() {
    use crayon_platform_windows::local_agent_ipc::{
        WindowsAgentIpcClient, WindowsAgentIpcEndpoint,
    };
    use std::sync::mpsc;

    let purpose = format!("agt12b-{}", std::process::id());
    let (path_tx, path_rx) = mpsc::channel();
    let server = std::thread::spawn(move || {
        let mut endpoint = WindowsAgentIpcEndpoint::new(&purpose).unwrap();
        endpoint.start().unwrap();
        path_tx.send(endpoint.pipe_path_for_connect()).unwrap();
        {
            let mut connection = CaapConnection::accept(
                &endpoint,
                SchemaVersion::CURRENT,
                vec![AgentCapability::PageRead],
            )
            .unwrap();
            let welcome = connection.handshake(0).unwrap();
            assert_eq!(welcome.capabilities(), &[AgentCapability::PageRead]);
            assert!(matches!(
                connection.receive(1),
                Ok(InboundMessage::Request(message)) if message.id() == 41
            ));
            assert_eq!(
                connection.receive(2),
                Ok(InboundMessage::Cancel(CaapCancel::new(41)))
            );
            connection.stop().unwrap();
        }
        endpoint.stop().unwrap();
        assert!(!endpoint.is_running());
    });

    let path = path_rx.recv().unwrap();
    let mut remote: Vec<u16> = r"\\remote-host\pipe\crayon-agent-test"
        .encode_utf16()
        .collect();
    remote.push(0);
    assert!(matches!(
        WindowsAgentIpcClient::connect(&remote),
        Err(LocalAgentIpcError::InvalidToken)
    ));
    let mut client = WindowsAgentIpcClient::connect(&path).unwrap();
    client
        .write_all(&json_frame(&hello(
            "windows-cli",
            vec![AgentCapability::PageRead],
        )))
        .unwrap();

    let mut response_header = [0u8; FRAME_HEADER_BYTES];
    client.read_exact(&mut response_header).unwrap();
    let response_len = u32::from_be_bytes(response_header) as usize;
    assert!(response_len <= MAX_FRAME_BYTES);
    let mut response = vec![0u8; response_len];
    client.read_exact(&mut response).unwrap();
    let welcome: CaapWelcome = serde_json::from_slice(&response).unwrap();
    assert_eq!(welcome.capabilities(), &[AgentCapability::PageRead]);

    assert!(matches!(
        WindowsAgentIpcClient::connect(&path),
        Err(LocalAgentIpcError::NotRunning)
    ));

    client.write_all(&json_frame(&request(41))).unwrap();
    client.write_all(&json_frame(&CaapCancel::new(41))).unwrap();
    drop(client);
    server.join().unwrap();
}

#[cfg(target_os = "macos")]
#[test]
fn macos_uds_runs_real_hello_request_cancel_flow() {
    use crayon_platform_macos::local_agent_ipc::MacUdsEndpoint;
    use std::os::unix::net::UnixStream;
    use std::sync::mpsc;

    let purpose = format!("agt12b-{}", std::process::id());
    let (path_tx, path_rx) = mpsc::channel();
    let server = std::thread::spawn(move || {
        let mut endpoint = MacUdsEndpoint::new(&purpose).unwrap();
        endpoint.start().unwrap();
        path_tx.send(endpoint.socket_path()).unwrap();
        {
            let mut connection = CaapConnection::accept(
                &endpoint,
                SchemaVersion::CURRENT,
                vec![AgentCapability::PageRead],
            )
            .unwrap();
            let welcome = connection.handshake(0).unwrap();
            assert_eq!(welcome.capabilities(), &[AgentCapability::PageRead]);
            assert!(matches!(
                connection.receive(1),
                Ok(InboundMessage::Request(message)) if message.id() == 41
            ));
            assert_eq!(
                connection.receive(2),
                Ok(InboundMessage::Cancel(CaapCancel::new(41)))
            );
            connection.stop().unwrap();
        }
        endpoint.stop().unwrap();
        assert!(!endpoint.is_running());
        assert!(!std::path::Path::new(&endpoint.socket_path()).exists());
    });

    let path = path_rx.recv().unwrap();
    let mut client = UnixStream::connect(&path).unwrap();
    client
        .write_all(&json_frame(&hello(
            "macos-cli",
            vec![AgentCapability::PageRead],
        )))
        .unwrap();

    let mut response_header = [0u8; FRAME_HEADER_BYTES];
    client.read_exact(&mut response_header).unwrap();
    let response_len = u32::from_be_bytes(response_header) as usize;
    assert!(response_len <= MAX_FRAME_BYTES);
    let mut response = vec![0u8; response_len];
    client.read_exact(&mut response).unwrap();
    let welcome: CaapWelcome = serde_json::from_slice(&response).unwrap();
    assert_eq!(welcome.capabilities(), &[AgentCapability::PageRead]);

    client.write_all(&json_frame(&request(41))).unwrap();
    client.write_all(&json_frame(&CaapCancel::new(41))).unwrap();
    drop(client);
    server.join().unwrap();
}
