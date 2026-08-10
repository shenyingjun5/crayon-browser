//! Stable product error codes returned by the Core (technical design §13).
//!
//! Codes are the compatibility contract: browser shells and adapters match on
//! the code string, never on a natural-language message. Adding a code is a
//! backward-compatible change; renaming or removing one is not.

use serde::{de::Error as _, Deserialize, Deserializer, Serialize, Serializer};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Stable Core error with a machine-readable code.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CoreError {
    /// Page-reported observation without browser-process verification.
    UntrustedObservation,
    /// No trusted user activation behind the playback.
    MissingUserActivation,
    /// Playback position did not advance.
    PlaybackNotAdvanced,
    /// Platform or receiver capabilities are not ready (fail closed).
    CapabilitiesUnavailable,
    /// DRM, EME, or private scrambling detected; direct cast is refused.
    DrmProtected,
    /// Receiver cannot play the candidate protocol/codec/resolution.
    ReceiverIncompatible,
    /// Media requires Cookie/Authorization that must never leave the browser.
    CredentialBoundMedia,
    /// Ad continuity unknown and the user chose from-the-start playback.
    AdContinuityUnknown,
    /// Site or media class is outside the direct-cast policy allow range.
    PolicyDenied,
    /// Peer schema version is outside the supported window.
    UnsupportedSchemaVersion,
    /// Wire message failed schema validation.
    InvalidMessage,
    /// Session token unknown or already revoked.
    SessionUnknown,
    /// Session exceeded its TTL.
    SessionExpired,
    /// Upstream host is outside the session allow-set or blocked by SSRF rules.
    UpstreamRejected,
}

impl CoreError {
    /// All stable codes, sorted; the golden contract asserts this exact set.
    pub const ALL: &[Self] = &[
        Self::UntrustedObservation,
        Self::MissingUserActivation,
        Self::PlaybackNotAdvanced,
        Self::CapabilitiesUnavailable,
        Self::DrmProtected,
        Self::ReceiverIncompatible,
        Self::CredentialBoundMedia,
        Self::AdContinuityUnknown,
        Self::PolicyDenied,
        Self::UnsupportedSchemaVersion,
        Self::InvalidMessage,
        Self::SessionUnknown,
        Self::SessionExpired,
        Self::UpstreamRejected,
    ];

    /// Stable machine-readable code (snake_case).
    #[must_use]
    pub const fn code(self) -> &'static str {
        match self {
            Self::UntrustedObservation => "untrusted_observation",
            Self::MissingUserActivation => "missing_user_activation",
            Self::PlaybackNotAdvanced => "playback_not_advanced",
            Self::CapabilitiesUnavailable => "capabilities_unavailable",
            Self::DrmProtected => "drm_protected",
            Self::ReceiverIncompatible => "receiver_incompatible",
            Self::CredentialBoundMedia => "credential_bound_media",
            Self::AdContinuityUnknown => "ad_continuity_unknown",
            Self::PolicyDenied => "policy_denied",
            Self::UnsupportedSchemaVersion => "unsupported_schema_version",
            Self::InvalidMessage => "invalid_message",
            Self::SessionUnknown => "session_unknown",
            Self::SessionExpired => "session_expired",
            Self::UpstreamRejected => "upstream_rejected",
        }
    }

    /// Resolves a stable code back to the error; unknown codes are rejected
    /// instead of being mapped to a catch-all.
    #[must_use]
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|e| e.code() == code)
    }
}

impl Display for CoreError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(self.code())
    }
}

impl Error for CoreError {}

impl Serialize for CoreError {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(self.code())
    }
}

impl<'de> Deserialize<'de> for CoreError {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let raw = String::deserialize(deserializer)?;
        Self::from_code(&raw)
            .ok_or_else(|| D::Error::custom(format!("unknown core error code: {raw}")))
    }
}
