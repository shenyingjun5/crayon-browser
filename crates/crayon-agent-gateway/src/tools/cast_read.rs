//! R0/R1 cast read tools (AGT-08): `cast.list_receivers` and
//! `cast.get_state`.
//!
//! The agent-visible surface is a closed, sanitized DTO set — receiver
//! IP addresses, media URLs, route/resource tokens and session material
//! are not expressible in these types.  Live data enters through the
//! [`CastReadSource`] port (implemented by app-runtime on top of the
//! Cast-SDK facade in a later wiring task); this layer only validates
//! bounds and renders deterministic snapshots.
//!
//! Generation values from the SDK capability cache are surfaced so
//! callers can fence against stale reads; the tool layer never caches.

use crayon_domain::CaapError;
use crayon_domain::ReceiverCapabilities;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum receivers one listing may contain (aligned with the SDK
/// capability cache bound).
pub const MAX_RECEIVERS: usize = 64;

/// Maximum device id length in bytes.
pub const MAX_DEVICE_ID_LEN: usize = 128;

/// Maximum display name length in bytes.
pub const MAX_DEVICE_NAME_LEN: usize = 128;

/// Closed cast playback states surfaced to agents.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CastPlaybackState {
    /// No cast session exists.
    Idle,
    /// A session is being established.
    Connecting,
    /// Media is playing on the receiver.
    Playing,
    /// Media is paused on the receiver.
    Paused,
    /// The session ended or was stopped.
    Stopped,
}

impl CastPlaybackState {
    /// Stable wire name used by snapshots.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Idle => "idle",
            Self::Connecting => "connecting",
            Self::Playing => "playing",
            Self::Paused => "paused",
            Self::Stopped => "stopped",
        }
    }
}

/// Read failure raised by the port or by tool-layer validation.
/// Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CastReadError {
    /// The cast stack cannot serve reads right now.
    SourceUnavailable,
    /// The source returned data violating closed bounds or shape.
    InvalidDeviceData,
    /// The receiver listing exceeds the bounded capacity.
    CapacityExceeded,
}

impl CastReadError {
    /// Stable mapping into CAAP error codes: an unavailable stack means
    /// the capability cannot be exercised; malformed source data is an
    /// internal message failure; overflow sheds like a full queue.
    #[must_use]
    pub const fn to_caap_error(self) -> CaapError {
        match self {
            Self::SourceUnavailable => CaapError::CapabilityDenied,
            Self::InvalidDeviceData => CaapError::InvalidMessage,
            Self::CapacityExceeded => CaapError::QueueFull,
        }
    }
}

impl Display for CastReadError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::SourceUnavailable => "cast read source is unavailable",
            Self::InvalidDeviceData => "cast read source returned invalid data",
            Self::CapacityExceeded => "receiver listing exceeds capacity",
        };
        formatter.write_str(message)
    }
}

impl Error for CastReadError {}

/// One discovered receiver as the agent may see it: opaque id, display
/// name and media capabilities.  No network location is carried.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReceiverEntry {
    pub device_id: String,
    pub name: String,
    pub capabilities: ReceiverCapabilities,
}

/// Sanitized receiver listing with the generation it reflects.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReceiversSnapshot {
    pub generation: u64,
    pub receivers: Vec<ReceiverSummary>,
}

/// Agent-visible summary derived from an entry after validation.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReceiverSummary {
    pub device_id: String,
    pub name: String,
    pub capabilities: ReceiverCapabilities,
}

/// Whole-browser cast state snapshot.  `receiver_id` is the same opaque
/// token used in listings; no route, resource or URL material exists.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CastStateSnapshot {
    pub state: CastPlaybackState,
    pub receiver_id: Option<String>,
    pub position_ms: u64,
    pub duration_ms: u64,
    pub generation: u64,
}

/// Live read port over the cast stack.  Implementations must return the
/// latest generation they know (AG-007); the tool layer never caches.
pub trait CastReadSource {
    /// Lists currently visible receivers with their capabilities.
    fn list_receivers(&self) -> Result<Vec<ReceiverEntry>, CastReadError>;
    /// Returns the current whole-browser cast state; no session yields
    /// `Idle` rather than an error.
    fn get_state(&self) -> Result<CastStateSnapshot, CastReadError>;
}

fn valid_text(value: &str, max_len: usize) -> bool {
    !value.trim().is_empty()
        && value.len() <= max_len
        && !value.bytes().any(|byte| byte < 0x20 || byte == 0x7F)
}

/// Validates and summarizes the receiver listing (R0 `cast.list_receivers`).
pub fn list_receivers(
    source: &dyn CastReadSource,
    generation: u64,
) -> Result<ReceiversSnapshot, CastReadError> {
    let entries = source.list_receivers()?;
    if entries.len() > MAX_RECEIVERS {
        return Err(CastReadError::CapacityExceeded);
    }
    let mut summaries = Vec::with_capacity(entries.len());
    for entry in entries {
        if !valid_text(&entry.device_id, MAX_DEVICE_ID_LEN)
            || !valid_text(&entry.name, MAX_DEVICE_NAME_LEN)
        {
            return Err(CastReadError::InvalidDeviceData);
        }
        summaries.push(ReceiverSummary {
            device_id: entry.device_id,
            name: entry.name,
            capabilities: entry.capabilities,
        });
    }
    // Deterministic order: by (device_id), independent of discovery order.
    summaries.sort_by(|a, b| a.device_id.cmp(&b.device_id));
    Ok(ReceiversSnapshot {
        generation,
        receivers: summaries,
    })
}

/// Validates and returns the cast state snapshot (R0 `cast.get_state`).
pub fn get_state(source: &dyn CastReadSource) -> Result<CastStateSnapshot, CastReadError> {
    let mut snapshot = source.get_state()?;
    if let Some(receiver_id) = &snapshot.receiver_id {
        if !valid_text(receiver_id, MAX_DEVICE_ID_LEN) {
            return Err(CastReadError::InvalidDeviceData);
        }
    }
    snapshot.receiver_id = snapshot.receiver_id.take().map(|id| id.trim().to_owned());
    Ok(snapshot)
}

fn escape_snapshot_text(value: &str) -> String {
    value.replace('\\', "\\\\").replace('|', "\\|")
}

fn capabilities_wire(capabilities: &ReceiverCapabilities) -> String {
    format!(
        "mp4={},hls={},dash={},h264={},hevc={},av1={},max_height={}",
        capabilities.mp4(),
        capabilities.hls(),
        capabilities.dash(),
        capabilities.h264(),
        capabilities.hevc(),
        capabilities.av1(),
        capabilities.max_height()
    )
}

impl ReceiversSnapshot {
    /// Deterministic snapshot lines; display names escape `\` and `|`.
    #[must_use]
    pub fn snapshot(&self) -> String {
        let mut out = format!("generation={}\n", self.generation);
        for receiver in &self.receivers {
            out.push_str(&format!(
                "{}|{}|{}\n",
                escape_snapshot_text(&receiver.device_id),
                escape_snapshot_text(&receiver.name),
                capabilities_wire(&receiver.capabilities)
            ));
        }
        out
    }
}

impl CastStateSnapshot {
    /// Deterministic single-line snapshot; `receiver=none` when idle of
    /// any session-less state.
    #[must_use]
    pub fn snapshot(&self) -> String {
        let receiver = match &self.receiver_id {
            Some(id) => escape_snapshot_text(id),
            None => "none".to_owned(),
        };
        format!(
            "state={}|receiver={}|pos_ms={}|dur_ms={}|generation={}\n",
            self.state.wire_name(),
            receiver,
            self.position_ms,
            self.duration_ms,
            self.generation
        )
    }
}

#[cfg(test)]
#[path = "cast_read_tests.rs"]
mod tests;
