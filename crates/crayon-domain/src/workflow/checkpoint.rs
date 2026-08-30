//! Checkpoint schema and state machine (WFL-01).
//!
//! A checkpoint is a short-lived, minimal resume anchor for one task:
//! tab/generation/revision binding plus an opaque bounded payload. The
//! schema carries no secrets and no page content — the payload is an
//! opaque blob owned by the checkpoint layer (WFL-04 adds encryption and
//! the store). Consumption is single-use; expiry is injected-clock based.

use crate::ids::{SessionGeneration, TabId};
use crate::workflow::WORKFLOW_SCHEMA_VERSION;
use serde::{Deserialize, Serialize};

/// Maximum payload bytes of one checkpoint.
pub const MAX_CHECKPOINT_PAYLOAD_BYTES: usize = 4096;

/// Maximum TTL of one checkpoint, in milliseconds.
pub const MAX_CHECKPOINT_TTL_MS: u64 = 300_000;

/// Checkpoint failure. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CheckpointError {
    /// The TTL is zero, negative or beyond the maximum.
    TtlOutOfBounds,
    /// The payload exceeds the bound.
    PayloadTooLarge,
    /// The checkpoint is not live for this operation.
    NotLive,
}

impl std::fmt::Display for CheckpointError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::TtlOutOfBounds => {
                write!(
                    formatter,
                    "checkpoint TTL must be in (0, {MAX_CHECKPOINT_TTL_MS}] ms"
                )
            }
            Self::PayloadTooLarge => formatter.write_str("checkpoint payload exceeds the bound"),
            Self::NotLive => formatter.write_str("checkpoint is not live"),
        }
    }
}

impl std::error::Error for CheckpointError {}

/// Closed checkpoint states.
#[derive(Clone, Copy, Debug, Default, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CheckpointState {
    #[default]
    Live,
    Consumed,
    Expired,
    Discarded,
}

/// The frozen v1 checkpoint.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Checkpoint {
    pub schema_version: u32,
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub revision: u64,
    /// Opaque bounded payload; the checkpoint layer never interprets it.
    pub payload: Vec<u8>,
    pub state: CheckpointState,
    pub created_at_ms: u64,
    pub expires_at_ms: u64,
}

impl Checkpoint {
    /// Validates TTL bounds and payload size; wraps a live checkpoint.
    pub fn new(
        tab_id: TabId,
        generation: SessionGeneration,
        revision: u64,
        payload: Vec<u8>,
        created_at_ms: u64,
        expires_at_ms: u64,
    ) -> Result<Self, CheckpointError> {
        if expires_at_ms <= created_at_ms || expires_at_ms - created_at_ms > MAX_CHECKPOINT_TTL_MS {
            return Err(CheckpointError::TtlOutOfBounds);
        }
        if payload.len() > MAX_CHECKPOINT_PAYLOAD_BYTES {
            return Err(CheckpointError::PayloadTooLarge);
        }
        Ok(Self {
            schema_version: WORKFLOW_SCHEMA_VERSION,
            tab_id,
            generation,
            revision,
            payload,
            state: CheckpointState::Live,
            created_at_ms,
            expires_at_ms,
        })
    }

    /// Whether the checkpoint is expired at the injected clock reading.
    #[must_use]
    pub fn expired_at(&self, now_ms: u64) -> bool {
        self.state == CheckpointState::Live && now_ms >= self.expires_at_ms
    }

    /// Consumes the checkpoint once; a consumed checkpoint can never be
    /// consumed again (single-use resume anchor).
    pub fn consume(&mut self, now_ms: u64) -> Result<(), CheckpointError> {
        if self.state != CheckpointState::Live {
            return Err(CheckpointError::NotLive);
        }
        if self.expired_at(now_ms) {
            self.state = CheckpointState::Expired;
            return Err(CheckpointError::NotLive);
        }
        self.state = CheckpointState::Consumed;
        Ok(())
    }

    /// Marks the checkpoint expired at the injected clock reading.
    pub fn mark_expired(&mut self, now_ms: u64) -> Result<(), CheckpointError> {
        if self.state != CheckpointState::Live || !self.expired_at(now_ms) {
            return Err(CheckpointError::NotLive);
        }
        self.state = CheckpointState::Expired;
        Ok(())
    }

    /// Discards the live checkpoint (user cancel, profile close).
    pub fn discard(&mut self) -> Result<(), CheckpointError> {
        if self.state != CheckpointState::Live {
            return Err(CheckpointError::NotLive);
        }
        self.state = CheckpointState::Discarded;
        Ok(())
    }
}
