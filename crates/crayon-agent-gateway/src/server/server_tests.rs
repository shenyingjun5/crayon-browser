//! AGT-12Ca server runtime tests: handshake-to-dispatch happy path,
//! chunk streaming with synthesized final, failed dispatch mapping,
//! cancel delivery, panic containment, sequential single-client serving
//! and cooperative stop. All tests are synchronous: client frames are
//! preloaded into the connection feed and `run` returns on EOF.

use super::{run, CaapDispatch, CancelFlag, DispatchOutcome, ServerExit, StopFlag};
use crate::transport::FrameCodec;
use crayon_domain::{AgentCapability, AgentTarget, CaapError};
use crayon_ipc_schema::{
    CaapCancel, CaapChunk, CaapErrorReply, CaapHello, CaapRequest, CaapWelcome, SchemaVersion,
};
use crayon_platform_api::local_agent_ipc::{
    LocalAgentIpcConnection, LocalAgentIpcEndpoint, LocalAgentIpcError, PeerIdentity,
};
use std::collections::{BTreeMap, VecDeque};
use std::io::{Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

const SCHEMA: SchemaVersion = SchemaVersion::CURRENT;

fn json_frame<T: serde::Serialize>(value: &T) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(value).unwrap())
}

/// Decoded server output frames.
#[derive(Clone, Default)]
struct OutputProbe {
    bytes: Arc<Mutex<Vec<u8>>>,
}

impl OutputProbe {
    fn frames(&self) -> Vec<Vec<u8>> {
        let raw = self.bytes.lock().unwrap().clone();
        let mut frames = Vec::new();
        let mut offset = 0usize;
        while offset + 4 <= raw.len() {
            let len = u32::from_be_bytes([
                raw[offset],
                raw[offset + 1],
                raw[offset + 2],
                raw[offset + 3],
            ]) as usize;
            let start = offset + 4;
            let end = start + len;
            if end > raw.len() {
                break;
            }
            frames.push(raw[start..end].to_vec());
            offset = end;
        }
        frames
    }
}

/// Client->server input. EOF while the buffer is exhausted.
#[derive(Clone, Default)]
struct InputFeed {
    bytes: Arc<Mutex<Vec<u8>>>,
    read_pos: Arc<Mutex<usize>>,
}

impl InputFeed {
    fn append(&self, bytes: Vec<u8>) {
        self.bytes.lock().unwrap().extend_from_slice(&bytes);
    }
}

struct ServerSideConnection {
    input: InputFeed,
    probe: OutputProbe,
    closed: Arc<AtomicBool>,
}

impl Read for ServerSideConnection {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        let input = self.input.bytes.lock().unwrap();
        let mut pos = self.input.read_pos.lock().unwrap();
        let available = input.len().saturating_sub(*pos);
        if available == 0 {
            return Ok(0);
        }
        let count = available.min(buffer.len());
        buffer[..count].copy_from_slice(&input[*pos..*pos + count]);
        *pos += count;
        Ok(count)
    }
}

impl Write for ServerSideConnection {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        self.probe.bytes.lock().unwrap().extend_from_slice(buffer);
        Ok(buffer.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl LocalAgentIpcConnection for ServerSideConnection {
    fn peer_identity(&self) -> PeerIdentity {
        PeerIdentity::new(true, true)
    }

    fn close(&mut self) -> Result<(), LocalAgentIpcError> {
        self.closed.store(true, Ordering::Relaxed);
        Ok(())
    }
}

/// Queue endpoint: pops preloaded connections; an empty queue reports
/// NotRunning exactly like a host-stopped endpoint, so `run` exits cleanly
/// once every queued client has been served. Fully synchronous.
#[derive(Clone)]
struct QueueEndpoint {
    shared: Arc<QueueShared>,
}

struct QueueShared {
    queue: Mutex<QueueState>,
}

struct QueueState {
    pending: VecDeque<ServerSideConnection>,
}

impl LocalAgentIpcEndpoint for QueueEndpoint {
    fn start(&mut self) -> Result<(), LocalAgentIpcError> {
        Ok(())
    }

    fn stop(&mut self) -> Result<(), LocalAgentIpcError> {
        Ok(())
    }

    fn accept(&self) -> Result<Box<dyn LocalAgentIpcConnection + '_>, LocalAgentIpcError> {
        let mut state = self.shared.queue.lock().unwrap();
        if let Some(connection) = state.pending.pop_front() {
            return Ok(Box::new(connection));
        }
        Err(LocalAgentIpcError::NotRunning)
    }

    fn is_running(&self) -> bool {
        true
    }
}

impl QueueEndpoint {
    fn new() -> Self {
        Self {
            shared: Arc::new(QueueShared {
                queue: Mutex::new(QueueState {
                    pending: VecDeque::new(),
                }),
            }),
        }
    }

    /// Queues one client connection with preloaded frames; returns its
    /// output probe.
    fn push(&self, client_frames: &[Vec<u8>]) -> OutputProbe {
        let probe = OutputProbe::default();
        let feed = InputFeed::default();
        for frame in client_frames {
            feed.append(frame.clone());
        }
        let connection = ServerSideConnection {
            input: feed,
            probe: probe.clone(),
            closed: Arc::new(AtomicBool::new(false)),
        };
        self.shared
            .queue
            .lock()
            .unwrap()
            .pending
            .push_back(connection);
        probe
    }
}

/// Dispatch fake: fixed chunk plan plus cancel/panic/failure controls.
struct FakeDispatch {
    chunks: Vec<(String, bool)>,
    cancel_seen: Arc<AtomicBool>,
    panic_on_request_id: Option<u64>,
    fail_with: Option<CaapError>,
}

impl FakeDispatch {
    fn streaming_chunks() -> Self {
        Self::new(vec![
            ("part one ".to_owned(), false),
            ("part two".to_owned(), true),
        ])
    }

    fn single_final() -> Self {
        Self::new(vec![("done".to_owned(), true)])
    }

    fn new(chunks: Vec<(String, bool)>) -> Self {
        Self {
            chunks,
            cancel_seen: Arc::new(AtomicBool::new(false)),
            panic_on_request_id: None,
            fail_with: None,
        }
    }
}

impl CaapDispatch for FakeDispatch {
    fn open_client(&mut self, _client: &str, _schema: SchemaVersion, _granted: &[AgentCapability]) {
    }

    fn close_client(&mut self, _client: &str) {}

    fn dispatch(
        &mut self,
        _client: &str,
        request: &CaapRequest,
        cancel: &CancelFlag,
        sink: &mut dyn FnMut(CaapChunk),
    ) -> DispatchOutcome {
        if self.panic_on_request_id == Some(request.id()) {
            panic!("injected dispatch panic");
        }
        if let Some(error) = self.fail_with {
            return DispatchOutcome::Failed(error);
        }
        for (index, (data, is_final)) in self.chunks.clone().iter().enumerate() {
            if cancel.is_cancelled() {
                return DispatchOutcome::Failed(CaapError::Cancelled);
            }
            let chunk =
                CaapChunk::new(request.id(), (index + 1) as u32, data, *is_final).expect("chunk");
            sink(chunk);
        }
        DispatchOutcome::Completed
    }

    fn notify_cancel(&mut self, _client: &str, _request_id: u64) {
        self.cancel_seen.store(true, Ordering::SeqCst);
    }
}

fn sample_request(id: u64, tool: &str) -> CaapRequest {
    CaapRequest::new(
        id,
        tool,
        AgentTarget::ActiveTab,
        60_000,
        "idem-key",
        BTreeMap::new(),
    )
    .expect("request")
}

fn capabilities() -> Vec<AgentCapability> {
    vec![AgentCapability::PageRead, AgentCapability::Navigation]
}

fn client_hello() -> Vec<u8> {
    json_frame(&CaapHello::new(SCHEMA, "cli", vec![AgentCapability::PageRead]).unwrap())
}

fn decode<T: serde::de::DeserializeOwned>(probe: &OutputProbe, index: usize) -> T {
    let frame = probe.frames()[index].clone();
    serde_json::from_slice(&frame).expect("decoded reply")
}

#[test]
fn happy_path_welcome_chunked_response_and_sequential_accept() {
    let mut endpoint = QueueEndpoint::new();
    let probe1 = endpoint.push(&[
        client_hello(),
        json_frame(&sample_request(41, "content.read")),
    ]);
    let probe2 = endpoint.push(&[client_hello(), json_frame(&sample_request(42, "page.read"))]);

    let stop = StopFlag::new();
    let mut dispatch = FakeDispatch::streaming_chunks();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);

    let welcome: CaapWelcome = decode(&probe1, 0);
    assert_eq!(welcome.schema(), SCHEMA);
    let first: CaapChunk = decode(&probe1, 1);
    let second: CaapChunk = decode(&probe1, 2);
    assert_eq!((first.seq(), first.is_final()), (1, false));
    assert_eq!(
        (second.seq(), second.is_final(), second.data()),
        (2, true, "part two")
    );

    // The second client is served after the first disconnects, with the
    // same two-chunk plan (final chunk at index 3).
    let first: CaapChunk = decode(&probe2, 1);
    assert_eq!((first.seq(), first.is_final()), (1, false));
    let second: CaapChunk = decode(&probe2, 2);
    assert_eq!(
        (second.seq(), second.is_final(), second.data()),
        (2, true, "part two")
    );
}

#[test]
fn successful_dispatch_without_final_gets_synthesized_final() {
    let mut endpoint = QueueEndpoint::new();
    let probe = endpoint.push(&[client_hello(), json_frame(&sample_request(5, "page.read"))]);

    let mut dispatch = FakeDispatch::single_final();
    dispatch.chunks = vec![("only part".to_owned(), false)];
    let stop = StopFlag::new();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);

    assert!(probe.frames().len() >= 3);
    let synthesized: CaapChunk = decode(&probe, 2);
    assert_eq!((synthesized.seq(), synthesized.is_final()), (2, true));
}

#[test]
fn failed_dispatch_maps_to_stable_error_reply() {
    let mut endpoint = QueueEndpoint::new();
    let probe = endpoint.push(&[
        client_hello(),
        json_frame(&sample_request(7, "cast.control")),
    ]);

    let mut dispatch = FakeDispatch::single_final();
    dispatch.fail_with = Some(CaapError::CapabilityDenied);
    let stop = StopFlag::new();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);

    let error: CaapErrorReply = decode(&probe, 1);
    assert_eq!(error.id(), 7);
    assert_eq!(error.error(), CaapError::CapabilityDenied);
}

#[test]
fn dispatch_panic_degrades_to_stable_error() {
    let mut endpoint = QueueEndpoint::new();
    let probe = endpoint.push(&[client_hello(), json_frame(&sample_request(9, "page.read"))]);

    let mut dispatch = FakeDispatch::single_final();
    dispatch.panic_on_request_id = Some(9);
    let stop = StopFlag::new();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);

    let error: CaapErrorReply = decode(&probe, 1);
    assert_eq!(error.error(), CaapError::InvalidMessage);
}

#[test]
fn cancel_between_requests_reaches_notify() {
    let mut endpoint = QueueEndpoint::new();
    let probe = endpoint.push(&[
        client_hello(),
        json_frame(&CaapCancel::new(3)),
        json_frame(&sample_request(3, "page.read")),
    ]);

    let cancel_seen = Arc::new(AtomicBool::new(false));
    let mut dispatch = FakeDispatch::single_final();
    dispatch.cancel_seen = Arc::clone(&cancel_seen);
    let stop = StopFlag::new();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);
    assert!(cancel_seen.load(Ordering::SeqCst));

    // The request after the cancel still completes normally.
    let chunk: CaapChunk = decode(&probe, 1);
    assert!(chunk.is_final());
}

#[test]
fn stop_before_accept_exits_cleanly() {
    let mut endpoint = QueueEndpoint::new();
    let stop = StopFlag::new();
    stop.stop();
    let mut dispatch = FakeDispatch::single_final();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);
}

#[test]
fn released_endpoint_is_a_clean_exit() {
    // The host released the endpoint: not an error, the loop just ends.
    let mut endpoint = QueueEndpoint::new();
    let stop = StopFlag::new();
    let mut dispatch = FakeDispatch::single_final();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);
}

#[test]
fn handshake_failure_ends_connection_not_server() {
    let bad_hello = FrameCodec::encode(b"not json");
    let mut endpoint = QueueEndpoint::new();
    let _probe_bad = endpoint.push(&[bad_hello]);
    let probe_good = endpoint.push(&[client_hello(), json_frame(&sample_request(1, "page.read"))]);

    let mut dispatch = FakeDispatch::single_final();
    let stop = StopFlag::new();
    let exit = run(
        &mut endpoint,
        &mut dispatch,
        &stop,
        || 1_000,
        SCHEMA,
        capabilities(),
    );
    assert_eq!(exit, ServerExit::Stopped);
    let chunk: CaapChunk = decode(&probe_good, 1);
    assert_eq!(chunk.data(), "done");
}
