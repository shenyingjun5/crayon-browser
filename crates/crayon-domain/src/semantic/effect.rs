//! Effect verification schema (ACT-01).
//!
//! An `EffectReport` is the terminal result of one executed action. The
//! `indeterminate` outcome exists so uncertain side effects are never
//! silently retried or replayed (AC-008 owns the wait/idempotency policy).

use crate::ids::{SessionGeneration, TabId};
use crate::semantic::action::ActionKind;
use crate::semantic::node::SemanticNodeId;
use crate::semantic::{SemanticSchemaError, MAX_EFFECT_DETAIL_BYTES, SEMANTIC_MAP_SCHEMA_VERSION};
use serde::{Deserialize, Serialize};

/// Closed effect outcomes.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EffectOutcome {
    /// The intended effect was observed.
    Verified,
    /// The action executed but the intended effect did not occur.
    Failed,
    /// The effect could not be confirmed within the bounded wait.
    Indeterminate,
}

impl EffectOutcome {
    /// All v1 outcomes; the closed set locked by golden tests.
    pub const ALL: [Self; 3] = [Self::Verified, Self::Failed, Self::Indeterminate];
}

/// Closed reasons attached to non-verified outcomes.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EffectReason {
    /// The bounded wait elapsed without a decisive observation.
    Timeout,
    /// A navigation ended the action's page context.
    NavigationOccurred,
    /// The target element left the document.
    ElementDetached,
    /// A precondition no longer held at execution time.
    PreconditionViolated,
    /// No decisive evidence either way.
    Unknown,
}

impl EffectReason {
    /// All v1 reasons; the closed set locked by golden tests.
    pub const ALL: [Self; 5] = [
        Self::Timeout,
        Self::NavigationOccurred,
        Self::ElementDetached,
        Self::PreconditionViolated,
        Self::Unknown,
    ];
}

/// Terminal report of one action execution.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct EffectReport {
    pub schema_version: u32,
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub revision: u64,
    pub action: ActionKind,
    pub node: SemanticNodeId,
    pub outcome: EffectOutcome,
    /// Required for `failed`/`indeterminate`; absent when `verified`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub reason: Option<EffectReason>,
    /// Bounded, data-free detail; never a stack trace or a file path.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}

impl EffectReport {
    /// Validates outcome/reason pairing and bounds; wraps a report.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        tab_id: TabId,
        generation: SessionGeneration,
        revision: u64,
        action: ActionKind,
        node: SemanticNodeId,
        outcome: EffectOutcome,
        reason: Option<EffectReason>,
        detail: Option<String>,
    ) -> Result<Self, SemanticSchemaError> {
        match outcome {
            EffectOutcome::Verified if reason.is_some() => {
                return Err(SemanticSchemaError::InvalidOutcome);
            }
            EffectOutcome::Verified => {}
            _ if reason.is_none() => return Err(SemanticSchemaError::InvalidOutcome),
            _ => {}
        }
        if let Some(text) = &detail {
            if text.len() > MAX_EFFECT_DETAIL_BYTES {
                return Err(SemanticSchemaError::BoundExceeded("effect detail"));
            }
        }
        Ok(Self {
            schema_version: SEMANTIC_MAP_SCHEMA_VERSION,
            tab_id,
            generation,
            revision,
            action,
            node,
            outcome,
            reason,
            detail,
        })
    }
}
