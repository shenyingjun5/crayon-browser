//! Bounded action handle registry (ACT-03).
//!
//! The registry is the single owner of live action handles. Resolution and
//! consumption are pure state transitions over an injected clock; a stale
//! generation, a profile mismatch, an expired TTL, a consumed handle or a
//! nonce mismatch all fail closed. A consumption attempt with a mismatched
//! nonce additionally destroys the handle, so a guessed or replayed nonce
//! can never be retried.

use crate::handle::data::{ActionHandle, HandleIssueError, HandleNonce, ProfileScope};
use crate::handle::id::ActionHandleId;
use crayon_domain::{ActionKind, SemanticNodeId, SessionGeneration, TabId};
use std::collections::BTreeMap;

/// Maximum number of live handles owned by one registry.
pub const MAX_ACTIVE_HANDLES: usize = 256;

/// Issuance outcome of [`HandleRegistry::issue`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum IssueOutcome {
    Issued(ActionHandle),
    /// The registry is at [`MAX_ACTIVE_HANDLES`]; nothing was evicted.
    Saturated,
    /// The TTL was out of bounds.
    Rejected(HandleIssueError),
    /// The OS entropy source failed; no handle was minted.
    EntropyUnavailable,
}

/// Resolution result; every non-`Resolved` variant is a fail-closed denial.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Resolution {
    Resolved,
    /// Unknown id, already consumed, or destroyed by a nonce mismatch.
    Unknown,
    Expired,
    /// The page generation advanced after issuance.
    StaleGeneration,
    /// The request crossed the handle's profile boundary.
    ProfileMismatch,
    /// The presented one-time nonce does not match.
    NonceMismatch,
}

/// Consumption result of [`HandleRegistry::consume`]. Success yields the
/// owned handle exactly once; every replay sees [`ConsumeOutcome::Unknown`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ConsumeOutcome {
    Consumed(ActionHandle),
    Unknown,
    Expired,
    StaleGeneration,
    ProfileMismatch,
    NonceMismatch,
}

/// Single owner of live action handles for the semantic action layer.
#[derive(Debug, Default)]
pub struct HandleRegistry {
    handles: BTreeMap<ActionHandleId, ActionHandle>,
}

impl HandleRegistry {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Number of live handles.
    #[must_use]
    pub fn len(&self) -> usize {
        self.handles.len()
    }

    /// Whether no handle is live.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.handles.is_empty()
    }

    /// Mints and registers a fresh handle bound to the given target,
    /// generation, profile scope and TTL window.
    #[allow(clippy::too_many_arguments)]
    pub fn issue(
        &mut self,
        node: SemanticNodeId,
        kind: ActionKind,
        tab_id: TabId,
        generation: SessionGeneration,
        profile: ProfileScope,
        issued_at_ms: u64,
        expires_at_ms: u64,
    ) -> IssueOutcome {
        if self.handles.len() >= MAX_ACTIVE_HANDLES {
            return IssueOutcome::Saturated;
        }
        let id = match ActionHandleId::generate() {
            Ok(id) => id,
            Err(_) => return IssueOutcome::EntropyUnavailable,
        };
        let Some(nonce) = Self::mint_nonce() else {
            return IssueOutcome::EntropyUnavailable;
        };
        match ActionHandle::new(
            id,
            node,
            kind,
            tab_id,
            generation,
            profile,
            nonce,
            issued_at_ms,
            expires_at_ms,
        ) {
            Ok(handle) => {
                self.handles.insert(handle.id.clone(), handle.clone());
                IssueOutcome::Issued(handle)
            }
            Err(error) => IssueOutcome::Rejected(error),
        }
    }

    /// Resolves a handle against the requesting context and clock without
    /// consuming it. A tab mismatch is reported as [`Resolution::Unknown`]:
    /// a handle bound to another target must be indistinguishable from a
    /// fabricated id.
    #[must_use]
    pub fn resolve(
        &self,
        id: &ActionHandleId,
        nonce: HandleNonce,
        tab_id: &TabId,
        generation: SessionGeneration,
        profile: &ProfileScope,
        now_ms: u64,
    ) -> Resolution {
        let Some(handle) = self.handles.get(id) else {
            return Resolution::Unknown;
        };
        if handle.nonce != nonce {
            return Resolution::NonceMismatch;
        }
        if handle.expired_at(now_ms) {
            return Resolution::Expired;
        }
        if handle.generation != generation {
            return Resolution::StaleGeneration;
        }
        if handle.tab_id != *tab_id {
            return Resolution::Unknown;
        }
        if handle.profile != *profile {
            return Resolution::ProfileMismatch;
        }
        Resolution::Resolved
    }

    /// Consumes a resolved handle. A handle is single-use: the registry
    /// removes it before reporting success, so any replay sees
    /// [`ConsumeOutcome::Unknown`]. A nonce mismatch destroys the handle.
    pub fn consume(
        &mut self,
        id: &ActionHandleId,
        nonce: HandleNonce,
        tab_id: &TabId,
        generation: SessionGeneration,
        profile: &ProfileScope,
        now_ms: u64,
    ) -> ConsumeOutcome {
        match self.resolve(id, nonce, tab_id, generation, profile, now_ms) {
            Resolution::NonceMismatch => {
                // A wrong nonce may be a replay or a guessing attempt; the
                // handle dies either way.
                self.handles.remove(id);
                ConsumeOutcome::NonceMismatch
            }
            Resolution::Resolved => {
                match self.handles.remove(id) {
                    Some(handle) => ConsumeOutcome::Consumed(handle),
                    // Unreachable: `resolve` just found it, and nothing
                    // else runs in between.
                    None => ConsumeOutcome::Unknown,
                }
            }
            Resolution::Unknown => ConsumeOutcome::Unknown,
            Resolution::Expired => ConsumeOutcome::Expired,
            Resolution::StaleGeneration => ConsumeOutcome::StaleGeneration,
            Resolution::ProfileMismatch => ConsumeOutcome::ProfileMismatch,
        }
    }

    /// Drops every handle of a tab whose generation is older than the
    /// given one; returns how many handles were dropped. A same-or-newer
    /// generation re-read keeps its handles.
    pub fn invalidate_before_generation(
        &mut self,
        tab_id: &TabId,
        generation: SessionGeneration,
    ) -> usize {
        let before = self.handles.len();
        self.handles
            .retain(|_, handle| handle.tab_id != *tab_id || handle.generation >= generation);
        before - self.handles.len()
    }

    /// Drops every handle of one tab (navigation away, tab close).
    pub fn invalidate_tab(&mut self, tab_id: &TabId) -> usize {
        let before = self.handles.len();
        self.handles.retain(|_, handle| handle.tab_id != *tab_id);
        before - self.handles.len()
    }

    /// Drops every handle of one profile scope (profile switch/close).
    pub fn invalidate_profile(&mut self, profile: &ProfileScope) -> usize {
        let before = self.handles.len();
        self.handles.retain(|_, handle| handle.profile != *profile);
        before - self.handles.len()
    }

    /// Drops every expired handle at the injected clock reading; returns
    /// how many were dropped. Bounded work per call.
    pub fn sweep_expired(&mut self, now_ms: u64) -> usize {
        let before = self.handles.len();
        self.handles.retain(|_, handle| !handle.expired_at(now_ms));
        before - self.handles.len()
    }

    fn mint_nonce() -> Option<HandleNonce> {
        let mut bytes = [0u8; 8];
        getrandom::fill(&mut bytes).ok()?;
        Some(HandleNonce::new(u64::from_le_bytes(bytes)))
    }
}
