//! Stable product cast errors (CS-008).
//!
//! Every `CastFacade` failure is mapped onto the closed `CastError` enum.
//! Callers match on the variant or `code()`, never on an SDK natural-language
//! message. The mapping consults only the SDK's stable `ErrorKind` category
//! (mirrored by `SenderErrorKind`) and selected stable machine `code` strings
//! of the pinned Cast-SDK revision (`44c3a998`); `message` is never parsed.
//!
//! Codes follow the `CoreError` contract: adding a variant is
//! backward-compatible, renaming or removing one is not.

use serde::{de::Error as _, Deserialize, Deserializer, Serialize, Serializer};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Mirror of the pinned SDK's `cast_sender_core::ErrorKind` categories.
///
/// Variants must stay 1:1 with the SDK enum. The SDK-05 service performs the
/// conversion at the boundary; `src/error_tests.rs` pins the correspondence
/// against the real SDK type so a revision upgrade that changes `ErrorKind`
/// breaks the build instead of silently mismapping errors.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SenderErrorKind {
    Device,
    Network,
    Http,
    Image,
    Control,
    InvalidInput,
    State,
}

impl SenderErrorKind {
    /// All categories, matching the pinned SDK `ErrorKind` set.
    pub const ALL: &[Self] = &[
        Self::Device,
        Self::Network,
        Self::Http,
        Self::Image,
        Self::Control,
        Self::InvalidInput,
        Self::State,
    ];
}

/// Stable product error for every `CastFacade` failure (CS-008).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CastError {
    /// Device unknown, offline, or not matched by a cast code (CS-003).
    DeviceNotFound,
    /// Cast code format rejected. Never produced by `from_sender_error`:
    /// it originates from `CastCode` validation, and the SDK-07 call site
    /// contextually remaps `InvalidInput` failures of cast-code resolution
    /// to this variant (the SDK reports format and argument errors under
    /// the same code).
    InvalidCastCode,
    /// Invalid call argument (e.g. volume out of range, empty input).
    InvalidInput,
    /// Current state forbids the operation (e.g. delivery while no device
    /// is connected).
    InvalidState,
    /// No active cast session, or the session already terminated.
    NoActiveSession,
    /// Session reference belongs to an older generation; fenced (CS-006).
    StaleSessionGeneration,
    /// The cast session could not be started on the receiver.
    CastStartFailed,
    /// Receiver does not support the requested media/capability.
    UnsupportedByReceiver,
    /// Validated LAN route expired or was lost; re-discover and reconnect.
    RouteLost,
    /// No usable local LAN interface or other local network failure.
    NetworkUnavailable,
    /// Receiver unreachable (description fetch, local HTTP transport).
    ReceiverUnreachable,
    /// Receiver protocol/control command failure (UPnP/SOAP/CastExtension).
    ReceiverProtocol,
    /// Unclassified SDK failure; carries no natural-language detail.
    Internal,
}

impl CastError {
    /// All stable codes; the contract test asserts this exact set.
    pub const ALL: &[Self] = &[
        Self::DeviceNotFound,
        Self::InvalidCastCode,
        Self::InvalidInput,
        Self::InvalidState,
        Self::NoActiveSession,
        Self::StaleSessionGeneration,
        Self::CastStartFailed,
        Self::UnsupportedByReceiver,
        Self::RouteLost,
        Self::NetworkUnavailable,
        Self::ReceiverUnreachable,
        Self::ReceiverProtocol,
        Self::Internal,
    ];

    /// Stable machine-readable code (snake_case).
    #[must_use]
    pub const fn code(self) -> &'static str {
        match self {
            Self::DeviceNotFound => "device_not_found",
            Self::InvalidCastCode => "invalid_cast_code",
            Self::InvalidInput => "invalid_input",
            Self::InvalidState => "invalid_state",
            Self::NoActiveSession => "no_active_session",
            Self::StaleSessionGeneration => "stale_session_generation",
            Self::CastStartFailed => "cast_start_failed",
            Self::UnsupportedByReceiver => "unsupported_by_receiver",
            Self::RouteLost => "route_lost",
            Self::NetworkUnavailable => "network_unavailable",
            Self::ReceiverUnreachable => "receiver_unreachable",
            Self::ReceiverProtocol => "receiver_protocol",
            Self::Internal => "internal",
        }
    }

    /// Resolves a stable code back to the error; unknown codes are rejected
    /// instead of being mapped to a catch-all.
    #[must_use]
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|e| e.code() == code)
    }

    /// Maps one pinned-SDK failure onto a stable product error (CS-008).
    ///
    /// Only the error category and the stable machine `code` are consulted;
    /// the SDK natural-language `message` is never parsed. Unknown codes
    /// degrade to the category default, keeping the mapping total across
    /// SDK revisions.
    #[must_use]
    pub fn from_sender_error(kind: SenderErrorKind, code: &str) -> Self {
        match kind {
            SenderErrorKind::Device => Self::DeviceNotFound,
            SenderErrorKind::Network => match code {
                "SENDER_DEVICE_ROUTE_EXPIRED"
                | "NETWORK_ROUTE_LOST"
                | "NETWORK_ROUTE_TEMPORARILY_UNAVAILABLE" => Self::RouteLost,
                _ => Self::NetworkUnavailable,
            },
            SenderErrorKind::Http => Self::ReceiverUnreachable,
            // The product facade exposes no image delivery; image-category
            // failures mean the receiver/SDK path cannot serve the media.
            SenderErrorKind::Image => Self::UnsupportedByReceiver,
            SenderErrorKind::Control => match code {
                "CONTROL_CAST_EXTENSION_MISSING" => Self::UnsupportedByReceiver,
                _ => Self::ReceiverProtocol,
            },
            SenderErrorKind::InvalidInput => Self::InvalidInput,
            SenderErrorKind::State => match code {
                "CAST_SESSION_STALE_GENERATION" => Self::StaleSessionGeneration,
                "CAST_SESSION_NOT_FOUND" | "CAST_SESSION_ALREADY_TERMINATED" => {
                    Self::NoActiveSession
                }
                "CAST_SESSION_START_FAILED" | "SESSION_CONTROL_START_FAILED" => {
                    Self::CastStartFailed
                }
                _ => Self::InvalidState,
            },
        }
    }
}

impl Display for CastError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(self.code())
    }
}

impl Error for CastError {}

impl Serialize for CastError {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(self.code())
    }
}

impl<'de> Deserialize<'de> for CastError {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let raw = String::deserialize(deserializer)?;
        Self::from_code(&raw)
            .ok_or_else(|| D::Error::custom(format!("unknown cast error code: {raw}")))
    }
}

#[cfg(test)]
#[path = "error_tests.rs"]
mod tests;
