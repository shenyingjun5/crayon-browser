//! Action-level human handoff (ACT-11, AC-011).
//!
//! A handoff is the terminal, explainable result of an action that cannot
//! safely continue in automation: the user takes over, a challenge is
//! detected, or the binding is gone. There is no implicit retry and no
//! permission inheritance: a resumable handoff always demands a fresh
//! page read and a fresh user confirmation, and the execution gate
//! (ACT-07) structurally enforces both because every handle is consumed
//! exactly once.

use crayon_domain::{ActionKind, SemanticNodeId, SessionGeneration, TabId};
use serde::{Deserialize, Serialize};

/// Whether automation can resume after the takeover.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HandoffKind {
    /// The user may retry after a fresh read and a fresh confirmation.
    Recoverable,
    /// The binding is gone; no resume is expressible.
    Unrecoverable,
}

/// Closed handoff reasons. Adding one is backward-compatible; renaming or
/// removing one is not.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HandoffReason {
    /// The user asked to take over the action.
    UserTakeoverRequested,
    /// A challenge (CAPTCHA or similar) was detected; solving is forbidden.
    ChallengeDetected,
    /// The user confirmation expired before dispatch.
    ConfirmationExpired,
    /// The execution was interrupted before a terminal effect.
    ExecutionInterrupted,
    /// The bounded deadline elapsed without a terminal effect.
    DeadlineElapsed,
    /// The handle was already consumed; no retry exists.
    HandleConsumed,
    /// The page generation superseded the binding.
    GenerationSuperseded,
    /// The target left the document.
    TargetRemoved,
    /// The profile holding the grant was closed.
    ProfileClosed,
}

impl HandoffReason {
    /// All reasons; the closed set locked by golden tests.
    pub const ALL: [Self; 9] = [
        Self::UserTakeoverRequested,
        Self::ChallengeDetected,
        Self::ConfirmationExpired,
        Self::ExecutionInterrupted,
        Self::DeadlineElapsed,
        Self::HandleConsumed,
        Self::GenerationSuperseded,
        Self::TargetRemoved,
        Self::ProfileClosed,
    ];

    /// The frozen kind of this reason; the record cannot contradict it.
    #[must_use]
    pub const fn kind(self) -> HandoffKind {
        match self {
            Self::UserTakeoverRequested
            | Self::ChallengeDetected
            | Self::ConfirmationExpired
            | Self::ExecutionInterrupted
            | Self::DeadlineElapsed => HandoffKind::Recoverable,
            Self::HandleConsumed
            | Self::GenerationSuperseded
            | Self::TargetRemoved
            | Self::ProfileClosed => HandoffKind::Unrecoverable,
        }
    }
}

/// Terminal, explainable handoff result of one action attempt.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct HandoffRecord {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub node: SemanticNodeId,
    pub action: ActionKind,
    pub kind: HandoffKind,
    pub reason: HandoffReason,
    /// Recoverable handoffs always demand a fresh page read before any
    /// retry; unrecoverable ones cannot resume at all.
    pub requires_fresh_read: bool,
    /// Recoverable handoffs always demand a fresh user confirmation;
    /// prior confirmations are never inherited.
    pub requires_new_confirmation: bool,
}

impl HandoffRecord {
    /// Whether automation may attempt the action again after the demanded
    /// fresh read and confirmation.
    #[must_use]
    pub const fn resumable(&self) -> bool {
        matches!(self.kind, HandoffKind::Recoverable)
    }
}

/// Builds the record for one handoff; the kind is derived from the frozen
/// reason table and cannot be overridden.
#[must_use]
pub fn handoff(
    tab_id: TabId,
    generation: SessionGeneration,
    node: SemanticNodeId,
    action: ActionKind,
    reason: HandoffReason,
) -> HandoffRecord {
    let kind = reason.kind();
    let recoverable = kind == HandoffKind::Recoverable;
    HandoffRecord {
        tab_id,
        generation,
        node,
        action,
        kind,
        reason,
        requires_fresh_read: recoverable,
        requires_new_confirmation: recoverable,
    }
}
