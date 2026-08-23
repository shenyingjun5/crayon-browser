//! Capability descriptor schema for the Capability Hub (HUB-01).
//!
//! Closed source/trust/lifecycle/version vocabulary shared by the
//! registry, router and partner connector.  Precedence rule: an
//! existing registration may only be replaced by a descriptor whose
//! source precedence is greater than or equal to the current one — a
//! builtin capability can never be overridden by a personal skill or
//! partner package, and partner packages can never claim system trust.

use serde::{Deserialize, Serialize};

/// Maximum capability id length in bytes.
pub const MAX_CAPABILITY_ID_LEN: usize = 64;
/// Maximum capability version length in bytes.
pub const MAX_CAPABILITY_VERSION_LEN: usize = 32;
/// Maximum capability summary length in bytes.
pub const MAX_CAPABILITY_SUMMARY_LEN: usize = 256;

/// Closed capability origin with replacement precedence (`larger
/// wins`).
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CapabilitySource {
    /// Approved partner API/MCP connector packages.
    Partner = 0,
    /// Personal Site Skills saved by the user.
    PersonalSkill = 1,
    /// Compiled-in browser capabilities.
    Builtin = 2,
}

impl CapabilitySource {
    /// Replacement precedence; a registration may only be replaced by
    /// a source with `>=` precedence.
    #[must_use]
    pub const fn precedence(self) -> u8 {
        match self {
            Self::Partner => 0,
            Self::PersonalSkill => 1,
            Self::Builtin => 2,
        }
    }

    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Partner => "partner",
            Self::PersonalSkill => "personal_skill",
            Self::Builtin => "builtin",
        }
    }
}

/// Closed trust level.  Partner sources can never declare system
/// trust.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TrustLevel {
    Untrusted,
    UserApproved,
    System,
}

impl TrustLevel {
    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Untrusted => "untrusted",
            Self::UserApproved => "user_approved",
            Self::System => "system",
        }
    }
}

/// Closed lifecycle state; `Revoked` is terminal for the id+version.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum LifecycleState {
    Active,
    Disabled,
    Revoked,
}

impl LifecycleState {
    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Disabled => "disabled",
            Self::Revoked => "revoked",
        }
    }
}

/// Closed data-scope class declared by a capability.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DataScope {
    /// No user data access.
    LocalOnly,
    /// Reads page content through the bounded page-data surface.
    PageContent,
    /// Controls casting through the normal cast gates.
    CastControl,
    /// Sends data to one approved partner endpoint.
    ExternalEndpoint,
}

impl DataScope {
    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::LocalOnly => "local_only",
            Self::PageContent => "page_content",
            Self::CastControl => "cast_control",
            Self::ExternalEndpoint => "external_endpoint",
        }
    }
}

/// Closed capability schema failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CapabilitySchemaError {
    InvalidId,
    InvalidVersion,
    SummaryTooLong,
    TrustConflict,
}

impl std::fmt::Display for CapabilitySchemaError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let text = match self {
            Self::InvalidId => "capability id rejected",
            Self::InvalidVersion => "capability version rejected",
            Self::SummaryTooLong => "capability summary exceeds limit",
            Self::TrustConflict => "trust level contradicts the source",
        };
        f.write_str(text)
    }
}

impl std::error::Error for CapabilitySchemaError {}

/// Reports whether `value` uses the closed capability charset
/// `[a-z0-9_.:-]`.
#[must_use]
pub fn is_capability_token(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_CAPABILITY_ID_LEN
        && value.bytes().all(|b| {
            b.is_ascii_lowercase() || b.is_ascii_digit() || matches!(b, b'_' | b'.' | b':' | b'-')
        })
}

/// One registrable capability descriptor.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct CapabilityDescriptor {
    pub id: String,
    pub version: String,
    pub source: CapabilitySource,
    pub trust: TrustLevel,
    pub data_scope: DataScope,
    pub summary: String,
}

impl CapabilityDescriptor {
    /// Validates the descriptor against the closed schema rules.
    pub fn validate(&self) -> Result<(), CapabilitySchemaError> {
        if !is_capability_token(&self.id) {
            return Err(CapabilitySchemaError::InvalidId);
        }
        if !is_capability_token(&self.version) || self.version.len() > MAX_CAPABILITY_VERSION_LEN {
            return Err(CapabilitySchemaError::InvalidVersion);
        }
        if self.summary.len() > MAX_CAPABILITY_SUMMARY_LEN {
            return Err(CapabilitySchemaError::SummaryTooLong);
        }
        // Partner packages never carry system trust.
        if self.source == CapabilitySource::Partner && self.trust == TrustLevel::System {
            return Err(CapabilitySchemaError::TrustConflict);
        }
        Ok(())
    }

    /// Stable wire tag used by deterministic snapshots.
    #[must_use]
    pub fn wire_tag(&self) -> String {
        format!(
            "{}@{}:{}:{}:{}",
            self.id,
            self.version,
            self.source.wire_name(),
            self.trust.wire_name(),
            self.data_scope.wire_name()
        )
    }
}

#[cfg(test)]
#[path = "capability_tests.rs"]
mod tests;
