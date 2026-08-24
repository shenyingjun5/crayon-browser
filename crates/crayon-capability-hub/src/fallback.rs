//! Fallback re-authorization decision model (HUB-05).
//!
//! When executing on the selected route fails, the fallback chain from
//! the HUB-04 policy is advisory only: advancing to any next step is a
//! **fresh authorization decision**.  Nothing about the previous
//! attempt is inherited — semantic target, scope, grant, confirmation,
//! idempotency key and data preview must all be redone.  The mandatory
//! checklist below is that rule in typed form; it can never be trimmed.
//!
//! Side-effect safety preempts the chain: unknown side effects and known
//! irreversible commits stop automation unconditionally — a partner API
//! failure never silently degrades into web execution over uncertain
//! state.

use crate::policy::PolicyDecision;
use crate::router::RouteKind;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Side-effect state of the failed attempt, as asserted by the executor
/// (app-runtime).  `Unknown` means effects may or may not have landed.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SideEffectState {
    /// Nothing was executed or nothing left a trace.
    None,
    /// Effects were committed and their final state is known.
    Committed { reversible: bool },
    /// It cannot be determined what happened.
    Unknown,
}

/// One failed execution attempt on a route.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RouteAttempt {
    pub executed_kind: RouteKind,
    pub side_effects: SideEffectState,
}

/// Mandatory re-validation checklist applied to EVERY fallback step.
/// Order is stable and rendering is locked by tests; items are
/// deliberately not individually addressable — all six always apply.
pub const REAUTHORIZATION_CHECKLIST: [&str; 6] = [
    "semantic_target",
    "scope",
    "grant",
    "confirmation",
    "idempotency_key",
    "data_preview",
];

/// Closed fallback verdicts.  Every variant stops short of execution:
/// callers must satisfy the checklist through a fresh user-authorized
/// flow before acting.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum FallbackVerdict {
    /// Advance to `next` after completing the full re-authorization
    /// checklist for the new provider/path.
    Reauthorize { next: RouteKind },
    /// No automation-capable step remains before the human terminal:
    /// pause and hand the task to the user (checkpoint semantics belong
    /// to WFL).
    HandOver,
    /// Automation must stop; no fallback may be attempted.
    Stop { reason: StopReason },
}

impl FallbackVerdict {
    /// Deterministic wire line for snapshots and audit trails.
    #[must_use]
    pub fn snapshot_line(&self) -> String {
        match self {
            Self::Reauthorize { next } => format!("reauthorize|{}", next.wire_name()),
            Self::HandOver => "hand_over".to_owned(),
            Self::Stop { reason } => format!("stop|{}", reason.wire_name()),
        }
    }
}

/// Closed reasons automation stopped outright.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StopReason {
    /// Side effects cannot be determined; proceeding could duplicate or
    /// corrupt work.
    UnknownSideEffects,
    /// Known committed effects cannot be undone; only the user may decide
    /// what happens next.
    IrreversibleCommit,
    /// The policy chain has no remaining steps.
    ChainExhausted,
}

impl StopReason {
    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::UnknownSideEffects => "unknown_side_effects",
            Self::IrreversibleCommit => "irreversible_commit",
            Self::ChainExhausted => "chain_exhausted",
        }
    }
}

/// Input validation failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FallbackError {
    /// The attempt kind does not match the decision's selected route.
    RouteNotSelected,
}

impl Display for FallbackError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::RouteNotSelected => "attempt kind does not match the selected route",
        };
        formatter.write_str(message)
    }
}

impl Error for FallbackError {}

/// Evaluates the verdict after one failed attempt against the policy
/// decision it belongs to.  Deterministic: identical inputs produce an
/// identical verdict.
pub fn evaluate(
    decision: &PolicyDecision,
    attempt: &RouteAttempt,
) -> Result<FallbackVerdict, FallbackError> {
    let Some(selected) = &decision.selected else {
        return Err(FallbackError::RouteNotSelected);
    };
    if selected.kind != attempt.executed_kind {
        return Err(FallbackError::RouteNotSelected);
    }
    // Side-effect safety preempts everything else.
    match attempt.side_effects {
        SideEffectState::Unknown => {
            return Ok(FallbackVerdict::Stop {
                reason: StopReason::UnknownSideEffects,
            })
        }
        SideEffectState::Committed { reversible: false } => {
            return Ok(FallbackVerdict::Stop {
                reason: StopReason::IrreversibleCommit,
            })
        }
        SideEffectState::None | SideEffectState::Committed { reversible: true } => {}
    }
    // First remaining step decides; terminals map to their own verdicts.
    match decision.fallback.first() {
        None | Some(RouteKind::Reject) => Ok(FallbackVerdict::Stop {
            reason: StopReason::ChainExhausted,
        }),
        Some(RouteKind::HumanHandoff) => Ok(FallbackVerdict::HandOver),
        Some(next) => Ok(FallbackVerdict::Reauthorize { next: *next }),
    }
}

#[cfg(test)]
#[path = "fallback_tests.rs"]
mod tests;
