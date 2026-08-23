//! Deterministic capability registry (HUB-01).
//!
//! One authoritative registration per capability id.  Registration rules:
//!
//! - The first registration of an id wins.
//! - A replacement requires a source with `>=` precedence than the
//!   current one AND a different version — a builtin capability can
//!   never be overridden by a personal skill or partner package, and an
//!   identical re-registration is a stable rejection, never a silent
//!   overwrite.
//! - `Revoked` is terminal for the exact id+version: the pair can never
//!   be registered again; newer versions of the same id may register.
//!
//! The registry is synchronous and in-memory (v1 semantics), bounded,
//! with no locks/threads/IO/clock.  Router, policy, fallback and the
//! partner connector belong to later HUB tasks.

use crayon_domain::{CapabilityDescriptor, CapabilitySchemaError, LifecycleState};
use std::collections::BTreeMap;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum number of tracked capability ids.
pub const MAX_REGISTRATIONS: usize = 64;

/// Maximum number of revoked versions retained per id; further
/// revoke/re-register cycles on that id fail closed instead of dropping
/// tombstones.
pub const MAX_REVOKED_HISTORY_PER_ID: usize = 8;

/// Registry operation failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RegistryError {
    /// The descriptor failed the closed schema validation.
    InvalidDescriptor,
    /// The exact id+version is already the current registration.
    DuplicateRegistration,
    /// The id+version was revoked; revoked pairs are terminal.
    VersionRevoked,
    /// The registering source has lower precedence than the current one.
    Conflict,
    /// The id's revoked-version history is full; fail closed.
    RevocationHistoryFull,
    /// No current registration matches the given id+version.
    RegistrationUnknown,
    /// A lifecycle transition tried to leave the terminal `Revoked` state.
    LifecycleTerminal,
    /// The registry is full.
    Capacity,
}

impl Display for RegistryError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidDescriptor => "descriptor failed schema validation",
            Self::DuplicateRegistration => "id+version is already registered",
            Self::VersionRevoked => "id+version was revoked and is terminal",
            Self::Conflict => "source precedence is below the current registration",
            Self::RevocationHistoryFull => "revoked-version history for this id is full",
            Self::RegistrationUnknown => "no current registration matches id+version",
            Self::LifecycleTerminal => "lifecycle state is terminal",
            Self::Capacity => "capability registry capacity reached",
        };
        formatter.write_str(message)
    }
}

impl Error for RegistryError {}

impl From<CapabilitySchemaError> for RegistryError {
    fn from(_: CapabilitySchemaError) -> Self {
        Self::InvalidDescriptor
    }
}

/// Read-only view of one registration (current or revoked history).
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RegistrationView {
    pub descriptor: CapabilityDescriptor,
    pub state: LifecycleState,
}

#[derive(Clone)]
struct RegistrationRecord {
    descriptor: CapabilityDescriptor,
    state: LifecycleState,
    /// Versions previously revoked for this id, most recent last;
    /// retained so revoked pairs can never be resurrected.
    revoked_history: Vec<CapabilityDescriptor>,
}

impl RegistrationRecord {
    fn view(&self) -> RegistrationView {
        RegistrationView {
            descriptor: self.descriptor.clone(),
            state: self.state,
        }
    }

    fn history_view(&self, version: &str) -> Option<RegistrationView> {
        self.revoked_history
            .iter()
            .find(|descriptor| descriptor.version == version)
            .map(|descriptor| RegistrationView {
                descriptor: descriptor.clone(),
                state: LifecycleState::Revoked,
            })
    }
}

/// The deterministic capability registry.
#[derive(Clone, Default)]
pub struct CapabilityRegistry {
    records: BTreeMap<String, RegistrationRecord>,
}

impl CapabilityRegistry {
    /// An empty registry.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Registers or replaces a capability.  All rejections are stable and
    /// leave the registry unchanged.
    pub fn register(&mut self, descriptor: CapabilityDescriptor) -> Result<(), RegistryError> {
        descriptor.validate()?;
        if !self.records.contains_key(&descriptor.id) && self.records.len() >= MAX_REGISTRATIONS {
            return Err(RegistryError::Capacity);
        }
        let Some(record) = self.records.get_mut(&descriptor.id) else {
            self.records.insert(
                descriptor.id.clone(),
                RegistrationRecord {
                    descriptor,
                    state: LifecycleState::Active,
                    revoked_history: Vec::new(),
                },
            );
            return Ok(());
        };
        if descriptor.source.precedence() < record.descriptor.source.precedence() {
            return Err(RegistryError::Conflict);
        }
        if record
            .revoked_history
            .iter()
            .any(|d| d.version == descriptor.version)
        {
            return Err(RegistryError::VersionRevoked);
        }
        if record.descriptor.version == descriptor.version {
            return match record.state {
                LifecycleState::Revoked => Err(RegistryError::VersionRevoked),
                _ => Err(RegistryError::DuplicateRegistration),
            };
        }
        if record.state == LifecycleState::Revoked {
            // Archive the revoked version before it is superseded, so the
            // pair stays terminal.
            if record.revoked_history.len() >= MAX_REVOKED_HISTORY_PER_ID {
                return Err(RegistryError::RevocationHistoryFull);
            }
            record.revoked_history.push(record.descriptor.clone());
        }
        record.descriptor = descriptor;
        record.state = LifecycleState::Active;
        Ok(())
    }

    /// Revokes the exact id+version.  Revocation takes effect immediately
    /// and is terminal; repeated revocation of the same pair is an
    /// idempotent no-op.  Only the live version or an already-revoked
    /// archived version can be named.
    pub fn revoke(&mut self, id: &str, version: &str) -> Result<(), RegistryError> {
        let Some(record) = self.records.get_mut(id) else {
            return Err(RegistryError::RegistrationUnknown);
        };
        if record.descriptor.version == version {
            record.state = LifecycleState::Revoked;
            return Ok(());
        }
        if record.history_view(version).is_some() {
            return Ok(());
        }
        Err(RegistryError::RegistrationUnknown)
    }

    /// Enables or disables the live version.  Transitions are bound to
    /// the exact version so stale callers fail instead of acting on a
    /// replaced registration; leaving `Revoked` is impossible.
    pub fn set_enabled(
        &mut self,
        id: &str,
        version: &str,
        enabled: bool,
    ) -> Result<(), RegistryError> {
        let Some(record) = self.records.get_mut(id) else {
            return Err(RegistryError::RegistrationUnknown);
        };
        if record.descriptor.version != version {
            return Err(RegistryError::RegistrationUnknown);
        }
        match (record.state, enabled) {
            (LifecycleState::Revoked, _) => Err(RegistryError::LifecycleTerminal),
            (LifecycleState::Active, false) => {
                record.state = LifecycleState::Disabled;
                Ok(())
            }
            (LifecycleState::Disabled, true) => {
                record.state = LifecycleState::Active;
                Ok(())
            }
            (_, _) => Ok(()),
        }
    }

    /// The current registration of an id.
    #[must_use]
    pub fn find(&self, id: &str) -> Option<RegistrationView> {
        self.records.get(id).map(RegistrationRecord::view)
    }

    /// A specific version of an id: the live registration when versions
    /// match, otherwise the archived view of a revoked version.
    #[must_use]
    pub fn find_version(&self, id: &str, version: &str) -> Option<RegistrationView> {
        let record = self.records.get(id)?;
        if record.descriptor.version == version {
            return Some(record.view());
        }
        record.history_view(version)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.records.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.records.is_empty()
    }

    /// Deterministic snapshot lines, one per id in id order:
    /// `id|version|source|trust|data_scope|state`.  Free-text summaries are
    /// excluded so diagnostics never carry manifest prose.
    #[must_use]
    pub fn snapshot(&self) -> String {
        let mut out = String::new();
        for (id, record) in &self.records {
            out.push_str(&format!(
                "{}|{}|{}|{}|{}|{}\n",
                id,
                record.descriptor.version,
                record.descriptor.source.wire_name(),
                record.descriptor.trust.wire_name(),
                record.descriptor.data_scope.wire_name(),
                record.state.wire_name()
            ));
        }
        out
    }
}

#[cfg(test)]
#[path = "registry_tests.rs"]
mod tests;
