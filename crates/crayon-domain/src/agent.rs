//! CAAP v1 domain primitives (AGT-01): agent targets, capabilities, risk
//! levels and the stable error codes shared by the CLI and MCP adapters.
//!
//! Permanently forbidden capabilities — raw CDP/WebDriver, arbitrary
//! JavaScript, cookies/credentials, password/payment, file upload,
//! arbitrary file-system or network access — are NOT expressible in these
//! types.  The closed enums below are the complete v1 set; extending them
//! is a protocol-versioned change.

use crate::ids::TabId;
use serde::{Deserialize, Serialize};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Closed v1 capability classes an agent client may request.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AgentCapability {
    /// R1: read targets, titles, selections, structured snapshots and
    /// Markdown of pages.
    PageRead,
    /// R2: open/switch/close tabs, navigate, go back, reload, scroll.
    Navigation,
    /// R0/R1: read receiver capabilities and cast state.
    CastRead,
    /// R3: choose devices and control playback through the normal cast
    /// gates.
    CastControl,
    /// R4: invoke verified semantic actions (behind a dedicated security
    /// review).
    SemanticAction,
}

impl AgentCapability {
    /// All v1 capabilities; the closed set used by closure tests.
    pub const ALL: [Self; 5] = [
        Self::PageRead,
        Self::Navigation,
        Self::CastRead,
        Self::CastControl,
        Self::SemanticAction,
    ];

    /// The risk level governing this capability.
    #[must_use]
    pub const fn risk_level(self) -> RiskLevel {
        match self {
            Self::CastRead => RiskLevel::R0,
            Self::PageRead => RiskLevel::R1,
            Self::Navigation => RiskLevel::R2,
            Self::CastControl => RiskLevel::R3,
            Self::SemanticAction => RiskLevel::R4,
        }
    }
}

/// Closed risk levels for agent capabilities.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RiskLevel {
    R0,
    R1,
    R2,
    R3,
    R4,
}

/// Closed target of an agent request.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum AgentTarget {
    /// A specific tab by its browser-assigned identifier.
    Tab { tab: TabId },
    /// The currently active tab of the calling client's window context.
    ActiveTab,
}

/// Stable CAAP error codes.
///
/// Codes are the compatibility contract: clients match on the code string,
/// never on a natural-language message.  Adding a code is a
/// backward-compatible change; renaming or removing one is not.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CaapError {
    /// Peer schema version is outside the supported window.
    VersionUnsupported,
    /// The requested capability was not granted for this session.
    CapabilityDenied,
    /// The named tool is not in the registry.
    ToolUnknown,
    /// The target is malformed or does not exist.
    TargetInvalid,
    /// The target changed (navigation/generation) after the grant.
    TargetStale,
    /// The request was cancelled by the client or the user.
    Cancelled,
    /// The caller-supplied deadline elapsed.
    DeadlineExceeded,
    /// A bounded queue is full; the request was shed.
    QueueFull,
    /// The client failed handshake or holds no valid grant.
    Unauthorized,
    /// The wire message failed schema validation.
    InvalidMessage,
}

impl CaapError {
    /// All v1 error codes; the closed set locked by golden tests.
    pub const ALL: [Self; 10] = [
        Self::VersionUnsupported,
        Self::CapabilityDenied,
        Self::ToolUnknown,
        Self::TargetInvalid,
        Self::TargetStale,
        Self::Cancelled,
        Self::DeadlineExceeded,
        Self::QueueFull,
        Self::Unauthorized,
        Self::InvalidMessage,
    ];
}

impl Display for CaapError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::VersionUnsupported => "peer schema version is not supported",
            Self::CapabilityDenied => "capability is not granted for this session",
            Self::ToolUnknown => "tool is not in the registry",
            Self::TargetInvalid => "target is malformed or does not exist",
            Self::TargetStale => "target changed after the grant was issued",
            Self::Cancelled => "request was cancelled",
            Self::DeadlineExceeded => "deadline elapsed",
            Self::QueueFull => "queue is full",
            Self::Unauthorized => "client is not authorized",
            Self::InvalidMessage => "message failed schema validation",
        };
        formatter.write_str(message)
    }
}

impl Error for CaapError {}
