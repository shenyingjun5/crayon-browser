//! Workflow trace schema (WFL-01).
//!
//! A trace records only authorized steps: the semantic intent (opaque node
//! id, closed action kind, bounded summary) and the verified effect
//! outcome. No selectors, no field values, no page content.

use crate::semantic::{ActionKind, EffectOutcome, SemanticNodeId, SemanticSchemaError};
use crate::workflow::WORKFLOW_SCHEMA_VERSION;
use serde::{Deserialize, Serialize};

/// Maximum steps in one trace.
pub const MAX_TRACE_STEPS: usize = 64;

/// Maximum bytes of one step summary.
pub const MAX_TRACE_SUMMARY_BYTES: usize = 128;

/// Maximum bytes of one trace origin.
pub const MAX_TRACE_ORIGIN_BYTES: usize = 255;

/// Trace construction failure. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TraceError {
    OriginInvalid,
    StepBudgetExceeded,
    SummaryTooLong,
}

impl std::fmt::Display for TraceError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::OriginInvalid => formatter.write_str("trace origin failed the closed check"),
            Self::StepBudgetExceeded => formatter.write_str("trace step budget exceeded"),
            Self::SummaryTooLong => formatter.write_str("step summary exceeds the bound"),
        }
    }
}

impl std::error::Error for TraceError {}

/// One recorded step: semantic intent plus verified terminal effect.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TraceStep {
    pub node: SemanticNodeId,
    pub action: ActionKind,
    pub summary: String,
    pub outcome: EffectOutcome,
}

/// The frozen v1 trace of one authorized task attempt.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WorkflowTrace {
    pub schema_version: u32,
    /// Validated `http(s)` origin the task ran on.
    pub origin: String,
    pub steps: Vec<TraceStep>,
}

impl WorkflowTrace {
    /// Validates bounds and origin; wraps a trace.
    pub fn new(origin: String, steps: Vec<TraceStep>) -> Result<Self, TraceError> {
        if !crate::semantic::is_valid_origin(&origin) {
            return Err(TraceError::OriginInvalid);
        }
        if steps.len() > MAX_TRACE_STEPS {
            return Err(TraceError::StepBudgetExceeded);
        }
        for step in &steps {
            if step.summary.len() > MAX_TRACE_SUMMARY_BYTES {
                return Err(TraceError::SummaryTooLong);
            }
        }
        Ok(Self {
            schema_version: WORKFLOW_SCHEMA_VERSION,
            origin,
            steps,
        })
    }

    /// Validates a step against trace bounds.
    pub fn validate_step(node: &SemanticNodeId, summary: &str) -> Result<(), SemanticSchemaError> {
        let _ = node;
        if summary.len() > MAX_TRACE_SUMMARY_BYTES {
            return Err(SemanticSchemaError::BoundExceeded("trace step summary"));
        }
        Ok(())
    }
}
