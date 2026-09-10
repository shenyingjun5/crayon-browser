//! Trust policy for outbound connectors (HUB-10).
//!
//! The registry holds the host-verified allow entries — an entry exists
//! only after the product's install flow verified the connector's source
//! signature. This layer adds the closed policy on top: exact-version
//! matching (downgrades and re-builds are untrusted), per-connector
//! revocation and a global kill switch that outranks everything. Pure
//! in-memory policy: no network, no persistence, no crypto.

use std::collections::BTreeMap;

use crate::api::{ConnectorId, TrustPort};

/// Maximum allow entries; a full registry rejects new `allow` calls and
/// keeps the existing set (fail closed).
pub const MAX_TRUST_ENTRIES: usize = 256;

/// Why `is_trusted` refused a connector. Closed, content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UntrustedReason {
    /// Global kill switch engaged.
    KillSwitch,
    /// The connector was explicitly revoked.
    Revoked,
    /// No allow entry for this connector id.
    UnknownConnector,
    /// An allow entry exists but the version does not match exactly
    /// (downgrade, tampered re-build or incompatible upgrade).
    VersionMismatch,
}

/// One verified install entry: the exact version the host verified.
#[derive(Clone, Debug, Eq, PartialEq)]
struct AllowEntry {
    version: String,
}

/// Trust registry implementing [`TrustPort`] with deny-by-default policy.
#[derive(Default)]
pub struct TrustRegistry {
    allowed: BTreeMap<String, AllowEntry>,
    revoked: BTreeMap<String, ()>,
    kill_switch: bool,
}

impl TrustRegistry {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Records a host-verified install. Idempotent for the same id;
    /// re-allowing a revoked connector clears the revocation only when the
    /// kill switch is disengaged (the operator action order is explicit).
    pub fn allow(&mut self, id: &ConnectorId, version: &str) -> Result<(), TrustError> {
        if self.kill_switch {
            return Err(TrustError::KillSwitchEngaged);
        }
        if self.allowed.len() >= MAX_TRUST_ENTRIES && !self.allowed.contains_key(id.key().as_str())
        {
            return Err(TrustError::CapacityExceeded);
        }
        if version.is_empty() {
            return Err(TrustError::InvalidVersion);
        }
        self.allowed.insert(
            id.key().to_owned(),
            AllowEntry {
                version: version.to_owned(),
            },
        );
        self.revoked.remove(id.key().as_str());
        Ok(())
    }

    /// Revokes one connector. Idempotent; survives re-allow only until the
    /// operator re-allows explicitly (revoke is the runtime emergency
    /// action, allow is the operator recovery action).
    pub fn revoke(&mut self, id: &ConnectorId) {
        self.revoked.insert(id.key().to_owned(), ());
    }

    /// Revokes every connector at once.
    pub fn revoke_all(&mut self) -> usize {
        let count = self.allowed.len();
        for key in self.allowed.keys() {
            self.revoked.insert(key.clone(), ());
        }
        count
    }

    /// Engages or disengages the global kill switch. Engaged: every
    /// connector is untrusted. Disengaging does not re-trust revoked
    /// connectors.
    pub fn set_kill_switch(&mut self, engaged: bool) {
        self.kill_switch = engaged;
    }

    #[must_use]
    pub fn kill_switch_engaged(&self) -> bool {
        self.kill_switch
    }

    /// The policy verdict for one connector with diagnostics.
    pub fn verdict(&self, id: &ConnectorId, version: &str) -> Result<(), UntrustedReason> {
        if self.kill_switch {
            return Err(UntrustedReason::KillSwitch);
        }
        if self.revoked.contains_key(id.key().as_str()) {
            return Err(UntrustedReason::Revoked);
        }
        match self.allowed.get(id.key().as_str()) {
            None => Err(UntrustedReason::UnknownConnector),
            Some(entry) if entry.version == version => Ok(()),
            Some(_) => Err(UntrustedReason::VersionMismatch),
        }
    }

    /// The count of live allow entries (diagnostics).
    #[must_use]
    pub fn allow_count(&self) -> usize {
        self.allowed.len()
    }
}

impl TrustPort for TrustRegistry {
    fn is_trusted(&mut self, id: &ConnectorId, version: &str) -> bool {
        self.verdict(id, version).is_ok()
    }
}

/// Trust operation failure; content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TrustError {
    KillSwitchEngaged,
    CapacityExceeded,
    InvalidVersion,
}

#[cfg(test)]
mod trust_tests;
