//! R4 semantic action CAAP adapter (AGT-15).
//!
//! This layer validates the frozen `act.invoke(action_id!,args?)` request,
//! then delegates exactly once to the normal semantic-action use case. It
//! never sees selectors, DOM objects, JavaScript, credentials or files and
//! never interprets untrusted argument text as another tool invocation.

use crayon_domain::{AgentTarget, CaapError, EffectReport};
use crayon_ipc_schema::{CaapChunk, CaapRequest};
use crayon_semantic_action::{ActionHandleId, IdempotencyKey};

/// Frozen registry name of the only R4 v1 tool.
pub const SEMANTIC_INVOKE_TOOL: &str = "act.invoke";
/// A bounded non-sensitive text/option argument. Password, payment and file
/// targets are rejected behind [`SemanticActionPort`] using verified facts.
pub const MAX_SEMANTIC_ARGUMENT_BYTES: usize = 512;

/// Validated, transport-independent invocation passed to app-runtime.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SemanticInvokeRequest {
    pub request_id: u64,
    pub target: AgentTarget,
    pub deadline_ms: u64,
    pub idempotency_key: IdempotencyKey,
    pub action_id: ActionHandleId,
    /// Opaque untrusted data. The adapter never parses it as commands.
    pub argument: Option<String>,
}

/// Closed input failures; none carries attacker-controlled text.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SemanticInputError {
    InvalidEnvelope,
    WrongTool,
    MissingActionId,
    InvalidActionId,
    InvalidIdempotencyKey,
    UnexpectedParameter,
    ArgumentOutOfBounds,
}

impl SemanticInvokeRequest {
    /// Validates the CAAP request against the AGT-02 frozen parameter set.
    pub fn from_caap(request: &CaapRequest) -> Result<Self, SemanticInputError> {
        request
            .validate()
            .map_err(|_| SemanticInputError::InvalidEnvelope)?;
        if request.tool() != SEMANTIC_INVOKE_TOOL {
            return Err(SemanticInputError::WrongTool);
        }
        if request
            .params()
            .keys()
            .any(|key| !matches!(key.as_str(), "action_id" | "args"))
        {
            return Err(SemanticInputError::UnexpectedParameter);
        }
        let raw_id = request
            .params()
            .get("action_id")
            .ok_or(SemanticInputError::MissingActionId)?;
        let action_id =
            ActionHandleId::new(raw_id).map_err(|_| SemanticInputError::InvalidActionId)?;
        let idempotency_key = IdempotencyKey::new(request.idempotency_key())
            .map_err(|_| SemanticInputError::InvalidIdempotencyKey)?;
        let argument = request.params().get("args").cloned();
        if argument.as_ref().is_some_and(|value| {
            value.len() > MAX_SEMANTIC_ARGUMENT_BYTES
                || value
                    .chars()
                    .any(|ch| ch == '\0' || (ch.is_control() && !matches!(ch, '\n' | '\r' | '\t')))
        }) {
            return Err(SemanticInputError::ArgumentOutOfBounds);
        }
        Ok(Self {
            request_id: request.id(),
            target: request.target().clone(),
            deadline_ms: request.deadline_ms(),
            idempotency_key,
            action_id,
            argument,
        })
    }
}

/// Stable fail-closed outcomes produced by the Browser-owned execution port.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SemanticRejection {
    Unauthorized,
    TargetInvalid,
    TargetStale,
    SensitiveTarget,
    HiddenOrCrossOrigin,
    ConfirmationMissing,
    DeadlineExceeded,
    Cancelled,
    QueueFull,
}

/// Port to the existing ACT/app-runtime pipeline. Implementations resolve
/// verified facts, re-check risk/preconditions/confirmation, execute through
/// the normal use case and return its terminal effect. No locator API exists.
pub trait SemanticActionPort {
    fn invoke(
        &mut self,
        request: &SemanticInvokeRequest,
    ) -> Result<EffectReport, SemanticRejection>;
}

/// Validates and dispatches one CAAP request, returning one final bounded
/// chunk. A terminal `indeterminate` effect is transported as data and must
/// remain non-retryable behind the port/session idempotency fence.
pub fn invoke_caap(
    port: &mut dyn SemanticActionPort,
    request: &CaapRequest,
) -> Result<CaapChunk, CaapError> {
    let request = SemanticInvokeRequest::from_caap(request).map_err(input_to_caap_error)?;
    let report = port.invoke(&request).map_err(rejection_to_caap_error)?;
    let data = serde_json::to_string(&report).map_err(|_| CaapError::InvalidMessage)?;
    CaapChunk::new(request.request_id, 0, &data, true).map_err(|_| CaapError::QueueFull)
}

#[must_use]
pub const fn input_to_caap_error(error: SemanticInputError) -> CaapError {
    match error {
        SemanticInputError::WrongTool => CaapError::ToolUnknown,
        SemanticInputError::InvalidEnvelope
        | SemanticInputError::MissingActionId
        | SemanticInputError::InvalidActionId
        | SemanticInputError::InvalidIdempotencyKey
        | SemanticInputError::UnexpectedParameter
        | SemanticInputError::ArgumentOutOfBounds => CaapError::InvalidMessage,
    }
}

#[must_use]
pub const fn rejection_to_caap_error(rejection: SemanticRejection) -> CaapError {
    match rejection {
        SemanticRejection::Unauthorized => CaapError::Unauthorized,
        SemanticRejection::TargetInvalid => CaapError::TargetInvalid,
        SemanticRejection::TargetStale => CaapError::TargetStale,
        SemanticRejection::SensitiveTarget
        | SemanticRejection::HiddenOrCrossOrigin
        | SemanticRejection::ConfirmationMissing => CaapError::CapabilityDenied,
        SemanticRejection::DeadlineExceeded => CaapError::DeadlineExceeded,
        SemanticRejection::Cancelled => CaapError::Cancelled,
        SemanticRejection::QueueFull => CaapError::QueueFull,
    }
}
