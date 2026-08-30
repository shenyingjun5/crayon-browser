//! Pre-persistence workflow redaction barrier (WFL-07).
//!
//! Every parameter value is discarded regardless of its caller-provided
//! class. The output can express only bounded names and closed placeholders;
//! there is intentionally no raw or unkeyed value hash in v1.

use std::collections::BTreeSet;

use crayon_domain::{EffectOutcome, TraceStep, WorkflowTrace, WORKFLOW_SCHEMA_VERSION};
use serde::Serialize;

/// Maximum number of placeholders attached to one persisted trace.
pub const MAX_REDACTED_PARAMETERS: usize = 16;
/// Maximum parameter name length in bytes.
pub const MAX_PARAMETER_NAME_BYTES: usize = 32;

/// Closed semantic class retained after the value is destroyed.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ParameterClass {
    Text,
    Email,
    AccountIdentifier,
    Secret,
    UserContent,
    Url,
}

/// Borrowed raw input. It deliberately implements neither serialization nor
/// Debug/Clone so callers cannot accidentally treat it as persistable output.
pub struct WorkflowParameter<'a> {
    pub name: &'a str,
    pub value: &'a str,
    pub class: ParameterClass,
}

/// Persistable placeholder; no value-derived field exists.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct ParameterPlaceholder {
    pub name: String,
    pub class: ParameterClass,
}

/// The only output of the write barrier. Serializing it cannot include input
/// values because they are absent from the type.
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct RedactedWorkflow {
    pub trace: WorkflowTrace,
    pub parameters: Vec<ParameterPlaceholder>,
}

/// Stable failures carrying no caller-controlled content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RedactionError {
    WrongSchema,
    TraceNotVerified,
    TraceInvalid,
    ParameterCapacity,
    ParameterNameInvalid,
    DuplicateParameter,
}

impl std::fmt::Display for RedactionError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::WrongSchema => "workflow trace schema rejected",
            Self::TraceNotVerified => "workflow trace contains a non-verified step",
            Self::TraceInvalid => "workflow trace failed validation",
            Self::ParameterCapacity => "workflow parameter capacity exceeded",
            Self::ParameterNameInvalid => "workflow parameter name rejected",
            Self::DuplicateParameter => "workflow parameter name is duplicated",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for RedactionError {}

/// Rebuilds a verified trace and replaces every raw value with a closed
/// placeholder before any persistence API can receive it.
pub fn redact_for_persistence(
    trace: &WorkflowTrace,
    parameters: &[WorkflowParameter<'_>],
) -> Result<RedactedWorkflow, RedactionError> {
    if trace.schema_version != WORKFLOW_SCHEMA_VERSION {
        return Err(RedactionError::WrongSchema);
    }
    if parameters.len() > MAX_REDACTED_PARAMETERS {
        return Err(RedactionError::ParameterCapacity);
    }

    let mut rebuilt_steps = Vec::with_capacity(trace.steps.len());
    for step in &trace.steps {
        if step.outcome != EffectOutcome::Verified {
            return Err(RedactionError::TraceNotVerified);
        }
        rebuilt_steps.push(TraceStep {
            node: step.node.clone(),
            action: step.action,
            summary: crate::trace::action_summary(step.action).to_owned(),
            outcome: EffectOutcome::Verified,
        });
    }
    let rebuilt_trace = WorkflowTrace::new(trace.origin.clone(), rebuilt_steps)
        .map_err(|_| RedactionError::TraceInvalid)?;

    let mut seen = BTreeSet::new();
    let mut placeholders = Vec::with_capacity(parameters.len());
    for parameter in parameters {
        if !valid_parameter_name(parameter.name) {
            return Err(RedactionError::ParameterNameInvalid);
        }
        if !seen.insert(parameter.name) {
            return Err(RedactionError::DuplicateParameter);
        }
        // Deliberately never inspect or transform `value`: even benign values
        // are destroyed so a wrong class cannot widen persisted data.
        placeholders.push(ParameterPlaceholder {
            name: parameter.name.to_owned(),
            class: parameter.class,
        });
    }

    Ok(RedactedWorkflow {
        trace: rebuilt_trace,
        parameters: placeholders,
    })
}

fn valid_parameter_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= MAX_PARAMETER_NAME_BYTES
        && name.bytes().all(|byte| {
            byte.is_ascii_lowercase() || byte.is_ascii_digit() || matches!(byte, b'_' | b'.' | b'-')
        })
}
