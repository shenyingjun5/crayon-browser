//! Controlled execution approval (ACT-07, AC-007).
//!
//! The gate composes the frozen layers into the single approval decision
//! that reaches execution: single-use handle consumption, precondition
//! re-evaluation at execution time, monotonic risk assessment and the
//! user-confirmation binding. It is pure state over an injected clock;
//! the approved action carries its deadline and binding facts so the
//! executor cannot widen them. Effect verification belongs to ACT-08.

use crate::handle::{ConsumeOutcome, HandleNonce, HandleRegistry, ProfileScope};
use crate::precondition::{evaluate, PreconditionInput, PreconditionReport};
use crate::risk::{assess, RiskDecision, RiskFacts};
use crayon_domain::{
    ActionKind, ElementState, SemanticNodeId, SemanticNodeKind, SessionGeneration, TabId,
};
use serde::{Deserialize, Serialize};

/// Maximum length of a confirmation reference token.
pub const MAX_CONFIRMATION_REF_BYTES: usize = 128;

/// Invalid confirmation reference.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConfirmationRefError {
    Empty,
    TooLong,
    InvalidCharset,
}

impl std::fmt::Display for ConfirmationRefError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Empty => formatter.write_str("confirmation reference must not be empty"),
            Self::TooLong => {
                formatter.write_str("confirmation reference exceeds the maximum length")
            }
            Self::InvalidCharset => formatter
                .write_str("confirmation reference contains characters outside [A-Za-z0-9_-]"),
        }
    }
}

impl std::error::Error for ConfirmationRefError {}

/// Opaque reference to one user confirmation; minted and owned by the
/// agent gateway confirm UI (AGT-05). Execution without a reference is
/// structurally inexpressible at the gate.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct ConfirmationRef(String);

impl ConfirmationRef {
    /// Wraps a validated confirmation reference.
    pub fn new(raw: &str) -> Result<Self, ConfirmationRefError> {
        if raw.is_empty() {
            return Err(ConfirmationRefError::Empty);
        }
        if raw.len() > MAX_CONFIRMATION_REF_BYTES {
            return Err(ConfirmationRefError::TooLong);
        }
        if !raw
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
        {
            return Err(ConfirmationRefError::InvalidCharset);
        }
        Ok(Self(raw.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl TryFrom<String> for ConfirmationRef {
    type Error = ConfirmationRefError;

    fn try_from(raw: String) -> Result<Self, Self::Error> {
        Self::new(&raw)
    }
}

impl From<ConfirmationRef> for String {
    fn from(reference: ConfirmationRef) -> Self {
        reference.0
    }
}

/// Everything one approval decision runs against. Page state fields are
/// current verified facts; the bound fields come from the verified map the
/// handle was issued for.
pub struct ExecutionRequest<'a> {
    pub handle_id: &'a crate::handle::ActionHandleId,
    pub nonce: HandleNonce,
    pub tab_id: &'a TabId,
    pub generation: SessionGeneration,
    pub profile: &'a ProfileScope,
    pub now_ms: u64,
    /// Origin and revision the verified map was produced at.
    pub bound_origin: &'a str,
    pub bound_revision: u64,
    /// Current verified page state.
    pub current_origin: &'a str,
    pub current_revision: u64,
    /// Verified state of the target node.
    pub kind: SemanticNodeKind,
    pub state: &'a ElementState,
    pub action: ActionKind,
    /// Whether discovery resolved exactly one target.
    pub unique_target: bool,
    /// Verified risk facts for the target.
    pub risk_facts: RiskFacts,
    /// The user confirmation bound to this execution.
    pub confirmation: Option<&'a ConfirmationRef>,
}

/// The single action that may now be performed through a normal browser
/// use case. Deadline and bindings are frozen here.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ApprovedAction {
    pub node: SemanticNodeId,
    pub action: ActionKind,
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    /// Execution must complete before this injected-clock reading.
    pub deadline_ms: u64,
    pub confirmation: ConfirmationRef,
}

/// Fail-closed approval denials. Every variant reports which layer denied;
/// none carries page content.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ApprovalOutcome {
    Approved(ApprovedAction),
    /// The handle layer denied (unknown/expired/stale/profile/nonce).
    HandleDenied(crate::handle::Resolution),
    /// Preconditions no longer hold; the caller must re-read the page.
    PreconditionViolated(PreconditionReport),
    /// The monotonic risk policy denied execution.
    RiskDenied(RiskDecision),
    /// No user confirmation was bound to the request.
    ConfirmationMissing,
}

/// Pure approval gate over its owned handle registry.
#[derive(Debug, Default)]
pub struct SemanticActionGate {
    registry: HandleRegistry,
}

impl SemanticActionGate {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Mutable access for lifecycle invalidation and sweeping.
    #[must_use]
    pub const fn registry(&mut self) -> &mut HandleRegistry {
        &mut self.registry
    }

    /// Runs the full fail-closed approval sequence exactly once per handle.
    pub fn approve(&mut self, request: ExecutionRequest<'_>) -> ApprovalOutcome {
        let Some(confirmation) = request.confirmation else {
            return ApprovalOutcome::ConfirmationMissing;
        };
        // Risk is assessed before consumption: a denied decision must not
        // burn the handle, so the caller can re-read and retry legally.
        let risk = assess(request.kind, request.risk_facts);
        if risk.denied() {
            return ApprovalOutcome::RiskDenied(risk);
        }
        let outcome = self.registry.consume(
            request.handle_id,
            request.nonce,
            request.tab_id,
            request.generation,
            request.profile,
            request.now_ms,
        );
        let handle = match outcome {
            ConsumeOutcome::Consumed(handle) => handle,
            ConsumeOutcome::Unknown => {
                return ApprovalOutcome::HandleDenied(crate::handle::Resolution::Unknown)
            }
            ConsumeOutcome::Expired => {
                return ApprovalOutcome::HandleDenied(crate::handle::Resolution::Expired)
            }
            ConsumeOutcome::StaleGeneration => {
                return ApprovalOutcome::HandleDenied(crate::handle::Resolution::StaleGeneration)
            }
            ConsumeOutcome::ProfileMismatch => {
                return ApprovalOutcome::HandleDenied(crate::handle::Resolution::ProfileMismatch)
            }
            ConsumeOutcome::NonceMismatch => {
                return ApprovalOutcome::HandleDenied(crate::handle::Resolution::NonceMismatch)
            }
        };
        let preconditions = evaluate(&PreconditionInput {
            kind: request.kind,
            state: request.state,
            action: request.action,
            bound_origin: request.bound_origin,
            current_origin: request.current_origin,
            bound_revision: request.bound_revision,
            current_revision: request.current_revision,
            unique_target: request.unique_target,
        });
        let preconditions = match preconditions {
            Ok(report) => report,
            Err(_) => return ApprovalOutcome::PreconditionViolated(PreconditionReport::default()),
        };
        if !preconditions.holds() {
            return ApprovalOutcome::PreconditionViolated(preconditions);
        }
        ApprovalOutcome::Approved(ApprovedAction {
            node: handle.node.clone(),
            action: request.action,
            tab_id: handle.tab_id.clone(),
            generation: handle.generation,
            deadline_ms: handle.expires_at_ms,
            confirmation: confirmation.clone(),
        })
    }
}
