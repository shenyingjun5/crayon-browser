//! Session secrets deliberately kept off the wire.
//!
//! Secrets are generated per process/session, authorise local IPC and media
//! routes, and must never appear in wire messages, logs, diagnostics, or
//! persisted state. `SessionSecret` therefore has no `Serialize`/`Deserialize`
//! implementation by design; serializable types reference sessions only by
//! id and generation (`SessionGrant`).

use crayon_domain::{SessionGeneration, SessionId};
use serde::{Deserialize, Serialize};
use std::fmt::{Debug, Formatter};

/// 256-bit per-session secret. Construction from CSPRNG bytes is the caller's
/// duty (relay/session tasks); this type only fixes ownership and redaction.
pub struct SessionSecret([u8; 32]);

impl SessionSecret {
    /// Secret length in bytes (256 bit entropy, RL-002 floor).
    pub const LENGTH: usize = 32;

    #[must_use]
    pub const fn from_bytes(bytes: [u8; Self::LENGTH]) -> Self {
        Self(bytes)
    }

    /// Raw bytes for local cryptographic use (HMAC signing etc.). Never copy
    /// these into serializable structures.
    #[must_use]
    pub const fn as_bytes(&self) -> &[u8; Self::LENGTH] {
        &self.0
    }
}

impl Debug for SessionSecret {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("SessionSecret(REDACTED)")
    }
}

impl Drop for SessionSecret {
    fn drop(&mut self) {
        self.0.fill(0);
    }
}

/// Serializable reference to a live session. Carries identity and generation
/// only — the authorising secret stays in the Core's in-memory vault.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SessionGrant {
    session_id: SessionId,
    generation: SessionGeneration,
}

impl SessionGrant {
    #[must_use]
    pub fn new(session_id: SessionId, generation: SessionGeneration) -> Self {
        Self {
            session_id,
            generation,
        }
    }

    #[must_use]
    pub const fn session_id(&self) -> &SessionId {
        &self.session_id
    }

    #[must_use]
    pub const fn generation(&self) -> SessionGeneration {
        self.generation
    }
}
