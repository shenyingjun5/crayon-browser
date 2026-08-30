//! Action handle data contract (ACT-03).
//!
//! One handle binds a single `action_id` to exactly one target node, one
//! action kind, one tab, one page generation, one profile scope, one TTL
//! window and one one-time nonce. Nothing else can resolve it; any
//! mismatch or replay fails closed.

use crate::handle::id::ActionHandleId;
use crayon_domain::{ActionKind, SemanticNodeId, SessionGeneration, TabId};
use serde::{Deserialize, Serialize};

/// Maximum TTL of one handle, in milliseconds; issuers may pick anything
/// in `(0, MAX_HANDLE_TTL_MS]`.
pub const MAX_HANDLE_TTL_MS: u64 = 300_000;

/// Maximum length of a profile scope token bound to a handle.
pub const MAX_PROFILE_SCOPE_BYTES: usize = 256;

/// Profile scope token validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProfileScopeError {
    Empty,
    TooLong,
}

impl std::fmt::Display for ProfileScopeError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Empty => formatter.write_str("profile scope must not be empty"),
            Self::TooLong => formatter.write_str("profile scope exceeds the maximum length"),
        }
    }
}

impl std::error::Error for ProfileScopeError {}

/// Validated profile scope bound to a handle; an opaque token owned by the
/// profile layer. Handles never cross this boundary.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct ProfileScope(String);

impl ProfileScope {
    /// Wraps a validated profile scope token.
    pub fn new(raw: &str) -> Result<Self, ProfileScopeError> {
        if raw.is_empty() {
            return Err(ProfileScopeError::Empty);
        }
        if raw.len() > MAX_PROFILE_SCOPE_BYTES {
            return Err(ProfileScopeError::TooLong);
        }
        Ok(Self(raw.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl TryFrom<String> for ProfileScope {
    type Error = ProfileScopeError;

    fn try_from(raw: String) -> Result<Self, Self::Error> {
        Self::new(&raw)
    }
}

impl From<ProfileScope> for String {
    fn from(scope: ProfileScope) -> Self {
        scope.0
    }
}

impl std::fmt::Display for ProfileScope {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// One-time 64-bit nonce bound at issue time; the consumer must present it
/// to consume the handle.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
pub struct HandleNonce(u64);

impl HandleNonce {
    /// Wraps a nonce value minted alongside the handle id.
    #[must_use]
    pub const fn new(value: u64) -> Self {
        Self(value)
    }

    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }
}

/// The frozen data carried by one issued action handle.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ActionHandle {
    pub id: ActionHandleId,
    pub node: SemanticNodeId,
    pub kind: ActionKind,
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub profile: ProfileScope,
    /// One-time consumption nonce; presentation mismatch invalidates.
    pub nonce: HandleNonce,
    pub issued_at_ms: u64,
    pub expires_at_ms: u64,
}

impl ActionHandle {
    /// Validates TTL bounds and wraps a handle. The explicit schema-field
    /// constructor keeps the frozen handle shape the single source of
    /// truth; callers assemble it from the issuing context.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        id: ActionHandleId,
        node: SemanticNodeId,
        kind: ActionKind,
        tab_id: TabId,
        generation: SessionGeneration,
        profile: ProfileScope,
        nonce: HandleNonce,
        issued_at_ms: u64,
        expires_at_ms: u64,
    ) -> Result<Self, HandleIssueError> {
        if expires_at_ms <= issued_at_ms || expires_at_ms - issued_at_ms > MAX_HANDLE_TTL_MS {
            return Err(HandleIssueError::TtlOutOfBounds);
        }
        Ok(Self {
            id,
            node,
            kind,
            tab_id,
            generation,
            profile,
            nonce,
            issued_at_ms,
            expires_at_ms,
        })
    }

    /// Whether the handle is expired at the injected clock reading.
    #[must_use]
    pub const fn expired_at(&self, now_ms: u64) -> bool {
        now_ms >= self.expires_at_ms
    }
}

/// Issuance rejection. Stable variants carry no internal detail.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandleIssueError {
    /// TTL is zero, negative or beyond [`MAX_HANDLE_TTL_MS`].
    TtlOutOfBounds,
}

impl std::fmt::Display for HandleIssueError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::TtlOutOfBounds => {
                write!(formatter, "TTL must be in (0, {MAX_HANDLE_TTL_MS}] ms")
            }
        }
    }
}

impl std::error::Error for HandleIssueError {}

/// External descriptor of an issued handle: identity, target and TTL only.
/// It carries no selector, no DOM reference and no page content.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ActionHandleDescriptor {
    pub id: ActionHandleId,
    pub node: SemanticNodeId,
    pub kind: ActionKind,
    pub expires_at_ms: u64,
}

impl ActionHandle {
    /// Bounded external view of this handle.
    #[must_use]
    pub fn descriptor(&self) -> ActionHandleDescriptor {
        ActionHandleDescriptor {
            id: self.id.clone(),
            node: self.node.clone(),
            kind: self.kind,
            expires_at_ms: self.expires_at_ms,
        }
    }
}
