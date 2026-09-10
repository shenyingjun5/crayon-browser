//! CAAP server runtime (AGT-12Ca).
//!
//! Owns the accept/serve loop around the AGT-12B connection runtime. The
//! loop is deliberately blocking and threading-free: the desktop host
//! (AGT-12Cc) decides how it is hosted, and tests drive it directly. Tool
//! dispatch happens behind [`CaapDispatch`]; grant, session and registry
//! policy arrive in AGT-12Cb. Payload bytes are never logged and stop is
//! cooperative: the flag is re-checked between every message.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use crayon_domain::{AgentCapability, CaapError};
use crayon_ipc_schema::{CaapChunk, CaapRequest, SchemaVersion};
use crayon_platform_api::local_agent_ipc::LocalAgentIpcError;

use crate::transport::{CaapConnection, ConnectionError, InboundMessage};

/// Cooperative shutdown flag shared with the hosting layer.
#[derive(Clone, Default)]
pub struct StopFlag(Arc<AtomicBool>);

impl StopFlag {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Requests a clean exit at the next loop boundary.
    pub fn stop(&self) {
        self.0.store(true, Ordering::SeqCst);
    }

    #[must_use]
    pub fn is_stopped(&self) -> bool {
        self.0.load(Ordering::SeqCst)
    }
}

/// Cancellation flag handed to an in-flight dispatch. Client cancel
/// requests set it; the dispatch implementation polls at its own bounded
/// checkpoints.
#[derive(Clone, Default)]
pub struct CancelFlag(Arc<AtomicBool>);

impl CancelFlag {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn cancel(&self) {
        self.0.store(true, Ordering::SeqCst);
    }

    #[must_use]
    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::SeqCst)
    }
}

/// Outcome of one dispatched request.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DispatchOutcome {
    /// All chunks went through the sink, including the final one.
    Completed,
    /// Stable protocol failure; the client receives the mapped error.
    Failed(CaapError),
}

/// Tool/session dispatch surface. Implementations run on the serve thread
/// and may block within the request deadline, polling [`CancelFlag`] at
/// their bounded checkpoints.
pub trait CaapDispatch {
    /// Streams response chunks through |sink|. The final chunk must carry
    /// `is_final`; the server synthesizes an empty final chunk when a
    /// successful dispatch forgot one.
    fn dispatch(
        &mut self,
        request: &CaapRequest,
        cancel: &CancelFlag,
        sink: &mut dyn FnMut(CaapChunk),
    ) -> DispatchOutcome;

    /// A cancel arrived for a request that is not in flight. Best-effort;
    /// late cancels are idempotent no-ops.
    fn notify_cancel(&mut self, request_id: u64);
}

/// Why the serve loop returned.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ServerExit {
    /// The stop flag was observed. The host stops the endpoint afterwards.
    Stopped,
    /// The endpoint failed before a connection was admitted.
    EndpointFailed,
}

/// Accept/serve loop. Sequential single-client: one connection is served to
/// completion before the next accept. Every connection ends with an
/// idempotent `stop`, including handshake failures and protocol strikes.
pub fn run<E, D, F>(
    endpoint: &mut E,
    dispatch: &mut D,
    stop: &StopFlag,
    clock: F,
    supported_schema: SchemaVersion,
    allowed_capabilities: Vec<AgentCapability>,
) -> ServerExit
where
    E: crayon_platform_api::local_agent_ipc::LocalAgentIpcEndpoint,
    // NotRunning (host stop) maps to a clean exit; see the match below.
    D: CaapDispatch,
    F: Fn() -> u64,
{
    while !stop.is_stopped() {
        match CaapConnection::accept(endpoint, supported_schema, allowed_capabilities.clone()) {
            Ok(mut connection) => {
                if serve(&mut connection, dispatch, stop, &clock) == ServeEnd::Stopped {
                    connection.stop().ok();
                    return ServerExit::Stopped;
                }
            }
            Err(ConnectionError::Endpoint(LocalAgentIpcError::NotRunning)) => {
                // The host stopped the endpoint: a clean end of life for
                // the loop, matching the flag-based stop.
                return ServerExit::Stopped;
            }
            Err(ConnectionError::Endpoint(_)) => {
                return if stop.is_stopped() {
                    ServerExit::Stopped
                } else {
                    ServerExit::EndpointFailed
                };
            }
            Err(_) => return ServerExit::EndpointFailed,
        }
    }
    ServerExit::Stopped
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ServeEnd {
    /// The client disconnected or the connection failed cleanly.
    Disconnected,
    /// The stop flag was observed mid-connection.
    Stopped,
}

fn serve<D, F>(
    connection: &mut CaapConnection<'_>,
    dispatch: &mut D,
    stop: &StopFlag,
    clock: &F,
) -> ServeEnd
where
    D: CaapDispatch,
    F: Fn() -> u64,
{
    if connection.handshake(clock()).is_err() {
        return ServeEnd::Disconnected;
    }
    loop {
        if stop.is_stopped() {
            return ServeEnd::Stopped;
        }
        match connection.receive(clock()) {
            Ok(InboundMessage::Request(request)) => {
                if dispatch_request(connection, dispatch, &request) == DispatchEnd::ConnectionDead {
                    return ServeEnd::Disconnected;
                }
            }
            Ok(InboundMessage::Cancel(cancel)) => dispatch.notify_cancel(cancel.id()),
            Err(ConnectionError::Closed | ConnectionError::Io)
            | Err(ConnectionError::Endpoint(_)) => {
                let _ = connection.stop();
                return ServeEnd::Disconnected;
            }
            Err(_) => {
                // Protocol-level rejection already produced a stable error
                // reply or a guard strike; keep serving — hostile input
                // must not terminate the client slot.
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum DispatchEnd {
    Completed,
    ConnectionDead,
}

fn dispatch_request<D>(
    connection: &mut CaapConnection<'_>,
    dispatch: &mut D,
    request: &CaapRequest,
) -> DispatchEnd
where
    D: CaapDispatch,
{
    let cancel = CancelFlag::new();
    let request_id = request.id();
    let mut last_seq: u32 = 0;
    let mut final_written = false;
    let mut connection_dead = false;
    {
        let mut sink = |chunk: CaapChunk| {
            last_seq = chunk.seq();
            final_written = chunk.is_final();
            if connection.write_chunk(&chunk).is_err() {
                connection_dead = true;
            }
        };
        // A panicking dispatch must not kill the product: it degrades to a
        // stable error reply, matching the fail-closed error surface.
        let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            dispatch.dispatch(request, &cancel, &mut sink)
        }))
        .unwrap_or(DispatchOutcome::Failed(CaapError::InvalidMessage));
        if connection_dead {
            return DispatchEnd::ConnectionDead;
        }
        match outcome {
            DispatchOutcome::Completed => {
                if !final_written {
                    let chunk = match CaapChunk::new(request_id, last_seq + 1, "", true) {
                        Ok(chunk) => chunk,
                        Err(_) => return DispatchEnd::ConnectionDead,
                    };
                    if connection.write_chunk(&chunk).is_err() {
                        return DispatchEnd::ConnectionDead;
                    }
                }
            }
            DispatchOutcome::Failed(error) => {
                if connection.write_error(request_id, error).is_err() {
                    return DispatchEnd::ConnectionDead;
                }
            }
        }
    }
    DispatchEnd::Completed
}

#[cfg(test)]
mod server_tests;
