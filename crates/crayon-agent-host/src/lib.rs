//! CAAP host for the desktop product (AGT-12Cc1).
//!
//! Wraps the agent-gateway server runtime in a process-hosted loop: one
//! serve thread bound to the platform endpoint, the gateway dispatch shared
//! behind a mutex so the product can issue grants when the user confirms
//! (AGT-05 flow), and a tool-execution port injected by the host. The C ABI
//! for embedding lives in the `ffi` module; everything payload-bearing
//! stays inside the gateway boundary — this layer owns lifecycle only.

pub mod ffi;

use std::sync::{Arc, Mutex};

use crayon_agent_gateway::grant::ProfileScope;
use crayon_agent_gateway::server::gateway::{GatewayConfig, GatewayDispatch, ToolPort};
use crayon_agent_gateway::server::{
    run, CaapDispatch, CancelFlag, DispatchOutcome, ServerExit, StopFlag,
};
use crayon_domain::{AgentCapability, AgentTarget, CaapError};
use crayon_ipc_schema::{CaapChunk, CaapRequest, SchemaVersion};
use crayon_platform_api::local_agent_ipc::{LocalAgentIpcEndpoint, LocalAgentIpcError};

/// Default UDS purpose token for the product endpoint.
pub const DEFAULT_PURPOSE: &str = "agent-caap";

/// Wall-clock milliseconds since the Unix epoch.
#[must_use]
pub fn wall_clock_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u64)
        .unwrap_or(0)
}

/// `GatewayDispatch` behind a mutex so confirmation-driven grant issuance
/// can interleave with the serve loop. The lock is held for the duration
/// of one dispatched request (or one grant issuance); a confirmation
/// arriving mid-request waits for the request's deadline or completion.
struct SharedDispatch<P: ToolPort> {
    inner: Arc<Mutex<GatewayDispatch<P>>>,
    active_client: Arc<Mutex<Option<String>>>,
}

impl<P: ToolPort> CaapDispatch for SharedDispatch<P> {
    fn open_client(&mut self, client: &str, schema: SchemaVersion, granted: &[AgentCapability]) {
        if let Ok(mut guard) = self.inner.lock() {
            guard.open_client(client, schema, granted);
        }
        if let Ok(mut active) = self.active_client.lock() {
            *active = Some(client.to_owned());
        }
    }

    fn close_client(&mut self, client: &str) {
        if let Ok(mut guard) = self.inner.lock() {
            guard.close_client(client);
        }
        if let Ok(mut active) = self.active_client.lock() {
            if active.as_deref() == Some(client) {
                *active = None;
            }
        }
    }

    fn dispatch(
        &mut self,
        client: &str,
        request: &CaapRequest,
        cancel: &CancelFlag,
        sink: &mut dyn FnMut(CaapChunk),
    ) -> DispatchOutcome {
        match self.inner.lock() {
            Ok(mut guard) => guard.dispatch(client, request, cancel, sink),
            Err(_) => DispatchOutcome::Failed(CaapError::Unauthorized),
        }
    }

    fn notify_cancel(&mut self, client: &str, request_id: u64) {
        if let Ok(mut guard) = self.inner.lock() {
            guard.notify_cancel(client, request_id);
        }
    }
}

/// Live host instance. Dropping it detaches (does not join) the serve
/// thread; call `stop_and_join` for a clean shutdown.
pub struct AgentHost<P: ToolPort> {
    shared: Arc<SharedDispatch<P>>,
    stop: StopFlag,
    serve: Option<std::thread::JoinHandle<ServerExit>>,
    purpose: String,
}

impl<P: ToolPort + Send + 'static> AgentHost<P> {
    /// Starts the serve loop on a caller-built endpoint. The endpoint must
    /// be freshly constructed; the host starts it and owns it inside the
    /// serve thread.
    pub fn start_with_endpoint<E>(
        mut endpoint: E,
        port: P,
        purpose: &str,
        profile: ProfileScope,
        grant_ttl_ms: u64,
        supported_schema: SchemaVersion,
        allowed_capabilities: Vec<AgentCapability>,
    ) -> Result<Self, LocalAgentIpcError>
    where
        E: LocalAgentIpcEndpoint + Send + 'static,
    {
        endpoint.start()?;
        let shared = Arc::new(SharedDispatch {
            inner: Arc::new(Mutex::new(GatewayDispatch::new(
                port,
                GatewayConfig {
                    profile,
                    grant_ttl_ms,
                },
                Box::new(wall_clock_ms),
            ))),
            active_client: Arc::new(Mutex::new(None)),
        });
        let stop = StopFlag::new();
        let thread_stop = stop.clone();
        let serve_shared = Arc::clone(&shared);
        let serve = std::thread::Builder::new()
            .name(format!("crayon-agent-host-{purpose}"))
            .spawn(move || {
                let mut shared_dispatch = SharedDispatch {
                    inner: Arc::clone(&serve_shared.inner),
                    active_client: Arc::clone(&serve_shared.active_client),
                };
                run(
                    &mut endpoint,
                    &mut shared_dispatch,
                    &thread_stop,
                    wall_clock_ms,
                    supported_schema,
                    allowed_capabilities,
                )
            })
            .map_err(|_| LocalAgentIpcError::NotRunning)?;
        Ok(Self {
            shared,
            stop,
            serve: Some(serve),
            purpose: purpose.to_owned(),
        })
    }

    /// The serve loop's stop flag.
    #[must_use]
    pub fn stop_flag(&self) -> &StopFlag {
        &self.stop
    }

    /// Purpose token of the hosted endpoint.
    #[must_use]
    pub fn purpose(&self) -> &str {
        &self.purpose
    }

    /// Issues a grant for the currently connected client (AGT-05
    /// confirmation flow). `target = None` grants profile-wide.
    pub fn issue_grant(
        &self,
        capability: AgentCapability,
        target: Option<AgentTarget>,
    ) -> Result<(), CaapError> {
        let client = self
            .shared
            .active_client
            .lock()
            .ok()
            .and_then(|active| active.clone())
            .ok_or(CaapError::Unauthorized)?;
        let mut guard = self
            .shared
            .inner
            .lock()
            .map_err(|_| CaapError::Unauthorized)?;
        guard.grant(&client, capability, target, wall_clock_ms())
    }

    /// Issues a grant for the active client without naming it (FFI path).
    pub fn issue_grant_to_active_client(
        &mut self,
        capability: AgentCapability,
    ) -> Result<(), CaapError> {
        let client = self
            .shared
            .active_client
            .lock()
            .ok()
            .and_then(|active| active.clone())
            .ok_or(CaapError::Unauthorized)?;
        let mut guard = self
            .shared
            .inner
            .lock()
            .map_err(|_| CaapError::Unauthorized)?;
        guard.grant(&client, capability, None, wall_clock_ms())
    }

    /// Requests a clean exit and waits up to |timeout| for the serve
    /// thread. A connection stuck in a blocking read keeps the loop alive
    /// until its client disconnects; past the timeout the thread is
    /// detached (it exits at the next loop boundary check).
    pub fn stop_and_join(mut self, timeout: std::time::Duration) -> Option<ServerExit> {
        self.stop.stop();
        let handle = self.serve.take()?;
        let deadline = std::time::Instant::now() + timeout;
        while std::time::Instant::now() < deadline {
            if handle.is_finished() {
                return handle.join().ok();
            }
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        None
    }
}

/// macOS UDS hosting convenience. Returns the live host plus the socket
/// path (for diagnostics and the C ABI surface).
#[cfg(target_os = "macos")]
pub fn start_macos_uds<P: ToolPort + Send + 'static>(
    purpose: &str,
    port: P,
    profile: ProfileScope,
    grant_ttl_ms: u64,
    supported_schema: SchemaVersion,
    allowed_capabilities: Vec<AgentCapability>,
) -> Result<(AgentHost<P>, String), LocalAgentIpcError> {
    let socket_path = {
        // The path derives purely from the purpose token; build a probe
        // endpoint only to compute it.
        use crayon_platform_macos::local_agent_ipc::MacUdsEndpoint;
        MacUdsEndpoint::new(purpose)?.socket_path()
    };
    let endpoint = crayon_platform_macos::local_agent_ipc::MacUdsEndpoint::new(purpose)?;
    let host = AgentHost::start_with_endpoint(
        endpoint,
        port,
        purpose,
        profile,
        grant_ttl_ms,
        supported_schema,
        allowed_capabilities,
    )?;
    Ok((host, socket_path))
}
