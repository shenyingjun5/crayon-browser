//! Deterministic, monotonic risk policy (ACT-06, AC-006).
//!
//! The policy derives a [`RiskDecision`] from verified Browser facts only.
//! There is no input path that can lower a level: the assessment accepts
//! facts, never verdicts, and every fact can only raise the outcome.
//! Sensitive elements (password/file) are excluded from execution
//! outright, so they can never produce an executable `action_id`.

use crayon_domain::{RiskLevel, RiskReason, SemanticNodeKind};
use serde::{Deserialize, Serialize};

/// Highest risk level still executable through the v1 action set without
/// escalation; R3/R4 decisions deny execution outright.
pub const MAX_EXECUTABLE_RISK: RiskLevel = RiskLevel::R2;

/// Verified context facts one assessment runs against. Every `true` can
/// only raise the risk; the struct has no lowering path.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct RiskFacts {
    /// The target sits in a payment context.
    pub payment_context: bool,
    /// The action navigates away from the current origin.
    pub offsite_navigation: bool,
    /// The action triggers a download.
    pub download_trigger: bool,
    /// The target lives in a cross-origin frame.
    pub cross_origin_frame: bool,
    /// Discovery matched multiple equally plausible targets.
    pub ambiguous_match: bool,
    /// Evidence confidence is below the execution bar.
    pub low_confidence: bool,
    /// The effect cannot be deterministically verified.
    pub unverified_effect: bool,
}

/// Deterministic outcome of one assessment.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RiskDecision {
    pub level: RiskLevel,
    pub reasons: Vec<RiskReason>,
    /// Whether the v1 action set may execute this target at all. Sensitive
    /// surfaces and R3/R4 decisions are never executable; confirmation and
    /// grant semantics stay owned by the agent gateway.
    pub executable: bool,
}

impl RiskDecision {
    /// Whether execution is denied outright.
    #[must_use]
    pub fn denied(&self) -> bool {
        !self.executable
    }
}

fn level_rank(level: &RiskLevel) -> u8 {
    match level {
        RiskLevel::R0 => 0,
        RiskLevel::R1 => 1,
        RiskLevel::R2 => 2,
        RiskLevel::R3 => 3,
        RiskLevel::R4 => 4,
    }
}

fn reason_level(reason: RiskReason) -> RiskLevel {
    match reason {
        RiskReason::SensitiveCredential | RiskReason::PaymentContext | RiskReason::FileUpload => {
            RiskLevel::R4
        }
        RiskReason::OffsiteNavigation
        | RiskReason::DownloadTrigger
        | RiskReason::CrossOriginFrame => RiskLevel::R3,
        RiskReason::AmbiguousMatch | RiskReason::LowConfidence | RiskReason::UnverifiedEffect => {
            RiskLevel::R2
        }
    }
}

/// Assesses one target/action pair deterministically: same facts, same
/// decision, always. `reasons` is bounded by the frozen reason vocabulary
/// and sorted for stable wire form.
#[must_use]
pub fn assess(kind: SemanticNodeKind, facts: RiskFacts) -> RiskDecision {
    let mut triggered: Vec<RiskReason> = Vec::new();
    if kind.sensitive() {
        let reason = if kind == SemanticNodeKind::PasswordInput {
            RiskReason::SensitiveCredential
        } else {
            RiskReason::FileUpload
        };
        triggered.push(reason);
    }
    let fact_reasons = [
        (facts.payment_context, RiskReason::PaymentContext),
        (facts.offsite_navigation, RiskReason::OffsiteNavigation),
        (facts.download_trigger, RiskReason::DownloadTrigger),
        (facts.cross_origin_frame, RiskReason::CrossOriginFrame),
        (facts.ambiguous_match, RiskReason::AmbiguousMatch),
        (facts.low_confidence, RiskReason::LowConfidence),
        (facts.unverified_effect, RiskReason::UnverifiedEffect),
    ];
    for (present, reason) in fact_reasons {
        if present {
            triggered.push(reason);
        }
    }
    triggered.sort();
    triggered.dedup();
    let mut level = RiskLevel::R0;
    for reason in &triggered {
        let candidate = reason_level(*reason);
        if level_rank(&candidate) > level_rank(&level) {
            level = candidate;
        }
    }
    let executable = !kind.sensitive() && level_rank(&level) <= level_rank(&MAX_EXECUTABLE_RISK);
    RiskDecision {
        level,
        reasons: triggered,
        executable,
    }
}
