//! Current-user local agent IPC endpoint (AG-012 gate semantics).
//!
//! Only same-user loopback peers may proceed to the CAAP handshake; every
//! other peer is rejected before any handshake bytes are processed, with
//! no browsing or network side effects.  `stop` is idempotent and
//! releases the endpoint.

use std::error::Error;
use std::fmt::{Display, Formatter};

/// Local agent IPC failure.  Variants are stable and carry no peer data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LocalAgentIpcError {
    /// The endpoint is not running.
    NotRunning,
    /// The peer failed the current-user loopback gate.
    PeerRejected,
    /// The CAAP handshake failed after admission.
    HandshakeFailed,
    /// The endpoint is already running.
    AlreadyRunning,
    /// A caller-supplied token violates the closed charset.
    InvalidToken,
}

impl Display for LocalAgentIpcError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::NotRunning => "local agent endpoint is not running",
            Self::PeerRejected => "local agent peer rejected before handshake",
            Self::HandshakeFailed => "local agent handshake failed",
            Self::AlreadyRunning => "local agent endpoint already running",
            Self::InvalidToken => "local agent endpoint token rejected",
        };
        formatter.write_str(message)
    }
}

impl Error for LocalAgentIpcError {}

/// Verified facts about a connecting peer, supplied by the platform
/// adapter (named-pipe impersonation / UDS peer credentials).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PeerIdentity {
    same_user: bool,
    loopback: bool,
}

impl PeerIdentity {
    #[must_use]
    pub const fn new(same_user: bool, loopback: bool) -> Self {
        Self {
            same_user,
            loopback,
        }
    }

    #[must_use]
    pub const fn same_user(self) -> bool {
        self.same_user
    }

    #[must_use]
    pub const fn is_loopback(self) -> bool {
        self.loopback
    }

    /// AG-012: only current-user loopback peers may proceed to the
    /// handshake.
    #[must_use]
    pub const fn handshake_allowed(self) -> bool {
        self.same_user && self.loopback
    }
}

/// Current-user local agent IPC endpoint.
pub trait LocalAgentIpcEndpoint: Send {
    /// Starts the endpoint with a current-user-only ACL.  Starting twice
    /// fails with `AlreadyRunning`; no remote or wildcard binding exists.
    fn start(&mut self) -> Result<(), LocalAgentIpcError>;

    /// Admits or rejects a connected peer.  Peers failing the
    /// current-user loopback gate are rejected before the handshake.
    fn admit_peer(&self, peer: PeerIdentity) -> Result<(), LocalAgentIpcError> {
        if !self.is_running() {
            return Err(LocalAgentIpcError::NotRunning);
        }
        if !peer.handshake_allowed() {
            return Err(LocalAgentIpcError::PeerRejected);
        }
        Ok(())
    }

    /// Stops the endpoint and releases the OS resource.  Idempotent:
    /// stopping a stopped endpoint succeeds.
    fn stop(&mut self) -> Result<(), LocalAgentIpcError>;

    #[must_use]
    fn is_running(&self) -> bool;
}

#[cfg(test)]
#[path = "local_agent_ipc_tests.rs"]
mod tests;
