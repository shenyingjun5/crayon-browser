//! Risk map schema (ACT-01).
//!
//! Risk in the semantic layer is deterministic and monotonic: the entries
//! here are decided by the frozen policy (ACT-06) from verified facts, and
//! page/model/connector input can never lower a level. This module freezes
//! only the data shapes.

use crate::agent::RiskLevel;
use crate::semantic::node::SemanticNodeId;
use crate::semantic::{SemanticSchemaError, MAX_RISK_ENTRIES, MAX_RISK_REASONS};
use serde::{Deserialize, Serialize};

/// Closed, stable risk reasons. Adding a reason is backward-compatible;
/// renaming or removing one is not.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RiskReason {
    /// The target is a credential entry surface.
    SensitiveCredential,
    /// The target sits in a payment context.
    PaymentContext,
    /// The target is a file upload surface.
    FileUpload,
    /// The action navigates away from the current origin.
    OffsiteNavigation,
    /// The action triggers a download.
    DownloadTrigger,
    /// The target lives in a cross-origin frame.
    CrossOriginFrame,
    /// Multiple equally plausible targets matched.
    AmbiguousMatch,
    /// Evidence confidence is below the execution bar.
    LowConfidence,
    /// The effect cannot be deterministically verified.
    UnverifiedEffect,
}

impl RiskReason {
    /// All v1 reasons; the closed set locked by golden tests.
    pub const ALL: [Self; 9] = [
        Self::SensitiveCredential,
        Self::PaymentContext,
        Self::FileUpload,
        Self::OffsiteNavigation,
        Self::DownloadTrigger,
        Self::CrossOriginFrame,
        Self::AmbiguousMatch,
        Self::LowConfidence,
        Self::UnverifiedEffect,
    ];
}

/// One node's frozen risk assessment: a monotonic level plus the closed
/// reasons that produced it.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RiskEntry {
    pub node: SemanticNodeId,
    pub level: RiskLevel,
    pub reasons: Vec<RiskReason>,
}

impl RiskEntry {
    /// Validates bounds and uniqueness; wraps a risk entry.
    pub fn new(
        node: SemanticNodeId,
        level: RiskLevel,
        reasons: Vec<RiskReason>,
    ) -> Result<Self, SemanticSchemaError> {
        if reasons.len() > MAX_RISK_REASONS {
            return Err(SemanticSchemaError::BudgetExceeded("risk reasons"));
        }
        let mut unique = reasons.clone();
        unique.sort();
        unique.dedup();
        if unique.len() != reasons.len() {
            return Err(SemanticSchemaError::DuplicateEntry("risk reason"));
        }
        Ok(Self {
            node,
            level,
            reasons,
        })
    }
}

/// Bounds check helper for the map assembly.
pub(crate) fn validate_risk(entries: &[RiskEntry]) -> Result<(), SemanticSchemaError> {
    if entries.len() > MAX_RISK_ENTRIES {
        return Err(SemanticSchemaError::BudgetExceeded("risk entries"));
    }
    Ok(())
}
