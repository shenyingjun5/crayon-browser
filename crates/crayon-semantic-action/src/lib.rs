//! Semantic action contracts (ACT).
//!
//! The `detail` module freezes the three bounded output profiles of the
//! frozen v1 semantic map: `compact`, `standard` and `internal_full`. Only
//! `compact`/`standard` leave the Browser process; `internal_full` is the
//! bounded internal profile consumed by engine-side semantic tasks and is
//! never equivalent to raw DOM. Every profile projects the same
//! platform-independent `crayon-domain` vocabulary — no selectors, no raw
//! HTML, no CDP, no field values, no credentials — and every collection is
//! fenced by named budgets that report truncation instead of failing open.

mod detail;
mod effect;
mod form;
mod handle;
mod precondition;
mod risk;
mod runtime;

pub use detail::{
    render_compact, render_internal_full, render_standard, CompactAction, CompactMap, CompactNode,
    DetailBudget, DetailProfile, InternalFullMap, SemanticNodeAnnotation, MAX_COMPACT_ACTIONS,
    MAX_COMPACT_NODES, MAX_STANDARD_ACTIONS, MAX_STANDARD_NODES,
};
pub use effect::{
    CheckOutcome, EffectLedger, EffectWaitSpec, IdempotencyKey, IdempotencyKeyError,
    MAX_EFFECT_LEDGER, MAX_EFFECT_WAIT_MS,
};
pub use form::{project_form, project_forms, FieldExclusion, FormFieldView, FormView};
pub use handle::{
    ActionHandle, ActionHandleDescriptor, ActionHandleId, ConsumeOutcome, HandleIdError,
    HandleIssueError, HandleNonce, HandleRegistry, IssueOutcome, ProfileScope, ProfileScopeError,
    Resolution, MAX_ACTIVE_HANDLES, MAX_HANDLE_TTL_MS,
};
pub use precondition::{
    evaluate, is_actionable, PreconditionCheck, PreconditionInput, PreconditionReport,
    PreconditionViolation, MAX_PRECONDITION_VIOLATIONS,
};
pub use risk::{assess, RiskDecision, RiskFacts, MAX_EXECUTABLE_RISK};
pub use runtime::{
    ApprovalOutcome, ApprovedAction, ConfirmationRef, ConfirmationRefError, ExecutionRequest,
    SemanticActionGate, MAX_CONFIRMATION_REF_BYTES,
};
