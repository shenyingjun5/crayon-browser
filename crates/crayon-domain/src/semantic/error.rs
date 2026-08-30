//! Semantic-layer error codes (ACT-01).
//!
//! Codes are the compatibility contract: clients match on the wire string,
//! never on a message. Adding a code is backward-compatible; renaming or
//! removing one is not. These codes are distinct from the CAAP transport
//! errors — they describe semantic-map and action-execution failures.

use serde::{Deserialize, Serialize};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Stable semantic error codes.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SemanticError {
    /// No verified map exists for the requested target/generation.
    MapUnavailable,
    /// The action handle is unknown or was already consumed.
    HandleUnknown,
    /// The action handle's TTL elapsed.
    HandleExpired,
    /// The page generation changed after the handle was issued.
    HandleStale,
    /// A frozen precondition no longer holds.
    PreconditionFailed,
    /// The target element cannot perform the requested action.
    NotActionable,
    /// The target is a sensitive surface excluded from execution.
    SensitiveTargetDenied,
    /// The requested revision is older than the owned state.
    RevisionStale,
    /// The request crossed a Profile boundary.
    ProfileMismatch,
    /// The effect ended as `indeterminate`; no replay was attempted.
    EffectIndeterminate,
    /// The effect ended as `failed`.
    EffectFailed,
}

impl SemanticError {
    /// All v1 codes; the closed set locked by golden tests.
    pub const ALL: [Self; 11] = [
        Self::MapUnavailable,
        Self::HandleUnknown,
        Self::HandleExpired,
        Self::HandleStale,
        Self::PreconditionFailed,
        Self::NotActionable,
        Self::SensitiveTargetDenied,
        Self::RevisionStale,
        Self::ProfileMismatch,
        Self::EffectIndeterminate,
        Self::EffectFailed,
    ];
}

impl Display for SemanticError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::MapUnavailable => "no verified semantic map for the target",
            Self::HandleUnknown => "action handle is unknown or consumed",
            Self::HandleExpired => "action handle TTL elapsed",
            Self::HandleStale => "page generation changed after handle issue",
            Self::PreconditionFailed => "frozen precondition does not hold",
            Self::NotActionable => "target cannot perform the requested action",
            Self::SensitiveTargetDenied => "target is excluded from execution",
            Self::RevisionStale => "requested revision is older than owned state",
            Self::ProfileMismatch => "request crossed a profile boundary",
            Self::EffectIndeterminate => "effect could not be verified",
            Self::EffectFailed => "action executed but the effect failed",
        };
        formatter.write_str(message)
    }
}

impl Error for SemanticError {}
