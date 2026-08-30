//! Authorized, verified and bounded workflow trace recorder (WFL-06).

use crayon_agent_gateway::receipt::{ActionReceipt, ReceiptOutcome};
use crayon_domain::{
    ActionKind, AgentCapability, EffectOutcome, EffectReport, SessionGeneration, TabId, TraceStep,
    WorkflowTrace, MAX_TRACE_STEPS, SEMANTIC_MAP_SCHEMA_VERSION,
};
use crayon_semantic_action::ApprovedAction;

/// Maximum lifetime of an in-memory trace attempt.
pub const MAX_TRACE_TTL_MS: u64 = 300_000;

/// Closed recording failure; no receipt, target or content is carried.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TraceRecorderError {
    InvalidTtl,
    Expired,
    Unauthorized,
    NotVerified,
    BindingMismatch,
    StaleResult,
    CapacityExceeded,
    InvalidOrigin,
}

impl std::fmt::Display for TraceRecorderError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidTtl => "trace TTL rejected",
            Self::Expired => "trace attempt expired",
            Self::Unauthorized => "authorized action evidence rejected",
            Self::NotVerified => "effect is not verified",
            Self::BindingMismatch => "trace binding mismatch",
            Self::StaleResult => "effect result is stale",
            Self::CapacityExceeded => "trace step capacity exceeded",
            Self::InvalidOrigin => "trace origin rejected",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for TraceRecorderError {}

/// Single-owner in-memory recorder for one task attempt.
pub struct TraceRecorder {
    origin: String,
    tab_id: TabId,
    generation: SessionGeneration,
    created_at_ms: u64,
    expires_at_ms: u64,
    last_revision: u64,
    steps: Vec<TraceStep>,
}

impl TraceRecorder {
    pub fn new(
        origin: &str,
        tab_id: TabId,
        generation: SessionGeneration,
        base_revision: u64,
        created_at_ms: u64,
        expires_at_ms: u64,
    ) -> Result<Self, TraceRecorderError> {
        if expires_at_ms <= created_at_ms || expires_at_ms - created_at_ms > MAX_TRACE_TTL_MS {
            return Err(TraceRecorderError::InvalidTtl);
        }
        WorkflowTrace::new(origin.to_owned(), Vec::new())
            .map_err(|_| TraceRecorderError::InvalidOrigin)?;
        Ok(Self {
            origin: origin.to_owned(),
            tab_id,
            generation,
            created_at_ms,
            expires_at_ms,
            last_revision: base_revision,
            steps: Vec::new(),
        })
    }

    /// Records one step only when grant receipt, approved action and verified
    /// effect agree. No caller-provided summary or action argument is accepted.
    pub fn record_verified(
        &mut self,
        receipt: &ActionReceipt,
        approved: &ApprovedAction,
        effect: &EffectReport,
        now_ms: u64,
    ) -> Result<(), TraceRecorderError> {
        if now_ms >= self.expires_at_ms {
            self.steps.clear();
            return Err(TraceRecorderError::Expired);
        }
        if receipt.tool() != "act.invoke"
            || receipt.capability() != AgentCapability::SemanticAction
            || receipt.outcome() != ReceiptOutcome::Succeeded
            || receipt.timestamp_ms() < self.created_at_ms
            || receipt.timestamp_ms() > now_ms
        {
            return Err(TraceRecorderError::Unauthorized);
        }
        let expected_target = format!("tab-{}", self.tab_id.as_str());
        if receipt.target() != "active" && receipt.target() != expected_target {
            return Err(TraceRecorderError::BindingMismatch);
        }
        if approved.tab_id != self.tab_id
            || approved.generation != self.generation
            || effect.schema_version != SEMANTIC_MAP_SCHEMA_VERSION
            || effect.tab_id != self.tab_id
            || effect.generation != self.generation
            || approved.node != effect.node
            || approved.action != effect.action
        {
            return Err(TraceRecorderError::BindingMismatch);
        }
        if now_ms > approved.deadline_ms {
            return Err(TraceRecorderError::Expired);
        }
        if effect.outcome != EffectOutcome::Verified || effect.reason.is_some() {
            return Err(TraceRecorderError::NotVerified);
        }
        if effect.revision <= self.last_revision {
            return Err(TraceRecorderError::StaleResult);
        }
        if self.steps.len() >= MAX_TRACE_STEPS {
            return Err(TraceRecorderError::CapacityExceeded);
        }

        self.steps.push(TraceStep {
            node: approved.node.clone(),
            action: approved.action,
            summary: action_summary(approved.action).to_owned(),
            outcome: EffectOutcome::Verified,
        });
        self.last_revision = effect.revision;
        Ok(())
    }

    /// Finalizes this attempt. Expired attempts yield no trace.
    pub fn finish(self, now_ms: u64) -> Result<WorkflowTrace, TraceRecorderError> {
        if now_ms >= self.expires_at_ms {
            return Err(TraceRecorderError::Expired);
        }
        WorkflowTrace::new(self.origin, self.steps).map_err(|error| match error {
            crayon_domain::TraceError::OriginInvalid => TraceRecorderError::InvalidOrigin,
            crayon_domain::TraceError::StepBudgetExceeded
            | crayon_domain::TraceError::SummaryTooLong => TraceRecorderError::CapacityExceeded,
        })
    }

    /// Cancels/fails the attempt and drops every in-memory step.
    #[must_use]
    pub fn discard(self) -> usize {
        self.steps.len()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.steps.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.steps.is_empty()
    }
}

const fn action_summary(action: ActionKind) -> &'static str {
    match action {
        ActionKind::Click => "click",
        ActionKind::SetText => "set_text",
        ActionKind::SelectOption => "select_option",
        ActionKind::Check => "check",
        ActionKind::Uncheck => "uncheck",
        ActionKind::Clear => "clear",
    }
}
