//! Controlled semantic action execution use case (ACT-07, AC-007).
//!
//! The runtime owns the approval gate and dispatches an approved action to
//! the injected executor port — the only path into normal browser use
//! cases. It keeps bounded counters, sweeps expired handles and shuts down
//! idempotently. Effect verification is owned by ACT-08; the executor
//! reports only whether the use case accepted the dispatch.

use crayon_semantic_action::{ApprovalOutcome, ExecutionRequest, SemanticActionGate};

use std::sync::atomic::{AtomicU64, Ordering};

/// Dispatch result of one request; every denial is fail closed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum DispatchOutcome {
    /// The executor accepted the dispatch; effect verification follows in
    /// the effect layer (ACT-08).
    Dispatched,
    /// The executor (normal browser use case) stably rejected the action.
    ExecutorRejected,
    /// The gate denied the request; the reason layer is reported.
    Denied(ApprovalOutcome),
}

/// Bounded lifetime counters; diagnostics only, never correctness inputs.
#[derive(Debug, Default)]
pub struct SemanticActionStats {
    pub requests: AtomicU64,
    pub approved: AtomicU64,
    pub dispatched: AtomicU64,
    pub denied: AtomicU64,
}

impl SemanticActionStats {
    /// Snapshot of the counters.
    #[must_use]
    pub fn snapshot(&self) -> (u64, u64, u64, u64) {
        (
            self.requests.load(Ordering::Relaxed),
            self.approved.load(Ordering::Relaxed),
            self.dispatched.load(Ordering::Relaxed),
            self.denied.load(Ordering::Relaxed),
        )
    }
}

/// Port to the normal browser use cases. Implementations run the real
/// action (click/text/select) and return whether they accepted it; they
/// must never see the handle, the page map or unverified page input.
pub trait ActionExecutor {
    fn perform(&self, action: &crayon_semantic_action::ApprovedAction) -> bool;
}

/// Single owner of semantic action approval and dispatch state.
#[derive(Debug, Default)]
pub struct SemanticActionRuntime {
    gate: SemanticActionGate,
    stats: SemanticActionStats,
}

impl SemanticActionRuntime {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Crate-internal access for lifecycle wiring and tests.
    pub(crate) fn gate_mut(&mut self) -> &mut SemanticActionGate {
        &mut self.gate
    }

    /// Approves and, when approved, dispatches exactly once to the
    /// executor. A denial consumes nothing beyond what the gate layer
    /// already consumed and is final for that handle.
    pub fn request(
        &mut self,
        request: ExecutionRequest<'_>,
        executor: &dyn ActionExecutor,
    ) -> DispatchOutcome {
        self.stats.requests.fetch_add(1, Ordering::Relaxed);
        match self.gate.approve(request) {
            ApprovalOutcome::Approved(action) => {
                self.stats.approved.fetch_add(1, Ordering::Relaxed);
                if executor.perform(&action) {
                    self.stats.dispatched.fetch_add(1, Ordering::Relaxed);
                    DispatchOutcome::Dispatched
                } else {
                    DispatchOutcome::ExecutorRejected
                }
            }
            denial @ ApprovalOutcome::HandleDenied(_)
            | denial @ ApprovalOutcome::PreconditionViolated(_)
            | denial @ ApprovalOutcome::RiskDenied(_)
            | denial @ ApprovalOutcome::ConfirmationMissing => {
                self.stats.denied.fetch_add(1, Ordering::Relaxed);
                DispatchOutcome::Denied(denial)
            }
        }
    }

    /// Drops expired handles at the injected clock reading; bounded work.
    pub fn sweep_expired(&mut self, now_ms: u64) -> usize {
        self.gate_mut().registry().sweep_expired(now_ms)
    }

    /// Invalidates every handle of one tab (navigation, close).
    pub fn invalidate_tab(&mut self, tab_id: &crayon_domain::TabId) -> usize {
        self.gate_mut().registry().invalidate_tab(tab_id)
    }

    /// Idempotent shutdown; drops all handle state.
    pub fn shut_down(&mut self) -> usize {
        let dropped = self.gate.registry().invalidate_all();
        dropped
    }

    /// Lifetime counter snapshot.
    #[must_use]
    pub fn stats(&self) -> (u64, u64, u64, u64) {
        self.stats.snapshot()
    }
}
