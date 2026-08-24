//! Default routing policy and override rules (HUB-04).
//!
//! Applies the frozen default policy
//! `Partner -> SiteSkill -> WebAutomation -> HumanHandoff -> Reject`
//! on top of the candidates resolved by the HUB-03 router, with exactly
//! two override rules: a user-preferred kind moves to the front, and the
//! data-exfiltration constraint can exclude external-endpoint scopes.
//! Untrusted candidates are never selected — only approved partners and
//! user-approved skills qualify.
//!
//! Unavailable paths (unknown/disabled/revoked ids) never reach this
//! layer: the router only emits live registrations as candidates.  The
//! fallback chain is advisory ordering for the NEXT authorization
//! decision — every step is a fresh grant/confirmation, never a
//! transparent retry; execution belongs to HUB-05.

use crate::router::{RouteCandidate, RouteDecision, RouteKind};
use crayon_domain::{DataScope, TrustLevel};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// User preference and constraints applied over the default rank.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PolicyPreferences {
    /// User-preferred route kind, moved to the front of the ranking.
    pub prefer_kind: Option<RouteKind>,
    /// When false, candidates declaring `DataScope::ExternalEndpoint` are
    /// excluded.
    pub allow_external_endpoint: bool,
}

impl Default for PolicyPreferences {
    fn default() -> Self {
        Self {
            prefer_kind: None,
            allow_external_endpoint: true,
        }
    }
}

impl PolicyPreferences {
    /// The frozen default policy without any override.
    #[must_use]
    pub const fn default_policy() -> Self {
        Self {
            prefer_kind: None,
            allow_external_endpoint: true,
        }
    }
}

/// Closed policy verdict for explainable route previews.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum PolicyReason {
    /// Nothing was applied yet (no policy evaluation ran).
    #[default]
    NotEvaluated,
    /// The default rank picked the winner.
    SelectedByDefaultRank,
    /// The user preference promoted the selected kind.
    SelectedByUserPreference,
    /// Every candidate failed the trust/data-scope gates.
    AllCandidatesExcluded,
    /// No candidate was resolved at all.
    NoCandidates,
}

impl PolicyReason {
    /// Stable wire name used by snapshots.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::NotEvaluated => "not_evaluated",
            Self::SelectedByDefaultRank => "selected_by_default_rank",
            Self::SelectedByUserPreference => "selected_by_user_preference",
            Self::AllCandidatesExcluded => "all_candidates_excluded",
            Self::NoCandidates => "no_candidates",
        }
    }
}

/// Closed reason why a resolved candidate was excluded.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExclusionReason {
    /// `TrustLevel::Untrusted` candidates are never selected.
    InsufficientTrust,
    /// External-endpoint scope while the constraint is off.
    ExternalDataForbidden,
}

impl ExclusionReason {
    /// Stable wire name used by snapshots.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::InsufficientTrust => "insufficient_trust",
            Self::ExternalDataForbidden => "external_data_forbidden",
        }
    }
}

/// One excluded candidate, identified by id with a closed reason.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Exclusion {
    pub capability_id: String,
    pub reason: ExclusionReason,
}

/// Policy output over one route decision.  `RouteDecision` stays the pure
/// resolution artifact; ownership of the judgment lives here.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct PolicyDecision {
    /// The winning candidate; `None` means Reject.
    pub selected: Option<RouteCandidate>,
    /// Remaining kinds in ascending rank order, always terminated by
    /// `HumanHandoff` then `Reject`.  Advisory only until HUB-05.
    pub fallback: Vec<RouteKind>,
    pub reason: PolicyReason,
    pub exclusions: Vec<Exclusion>,
}

/// Policy input failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PolicyError {
    /// `prefer_kind = Reject` is not a selectable preference.
    InvalidPreference,
}

impl Display for PolicyError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidPreference => "reject is not a valid preferred kind",
        };
        formatter.write_str(message)
    }
}

impl Error for PolicyError {}

/// Applies preferences to a resolved decision.  Deterministic: the same
/// decision and preferences always produce the same output.
pub fn apply(
    preferences: &PolicyPreferences,
    decision: &RouteDecision,
) -> Result<PolicyDecision, PolicyError> {
    if matches!(preferences.prefer_kind, Some(RouteKind::Reject)) {
        return Err(PolicyError::InvalidPreference);
    }

    // Gates first: untrusted is never selectable, and the data-exfiltration
    // constraint can exclude external endpoints.  Exclusions stay id-sorted.
    let mut exclusions = Vec::new();
    let mut viable: Vec<&RouteCandidate> = Vec::new();
    for candidate in &decision.candidates {
        if candidate.trust == TrustLevel::Untrusted {
            exclusions.push(Exclusion {
                capability_id: candidate.capability_id.clone(),
                reason: ExclusionReason::InsufficientTrust,
            });
        } else if !preferences.allow_external_endpoint
            && candidate.data_scope == DataScope::ExternalEndpoint
        {
            exclusions.push(Exclusion {
                capability_id: candidate.capability_id.clone(),
                reason: ExclusionReason::ExternalDataForbidden,
            });
        } else {
            viable.push(candidate);
        }
    }
    exclusions.sort_by(|a, b| a.capability_id.cmp(&b.capability_id));

    // Effective ranking: preferred kind first, remaining kinds keep the
    // frozen default order; ties break by id (candidates arrive sorted).
    let effective_rank = |kind: RouteKind| -> u8 {
        match preferences.prefer_kind {
            Some(preferred) if kind == preferred => 0,
            _ => kind.rank(),
        }
    };
    viable.sort_by(|a, b| {
        (effective_rank(a.kind), a.capability_id.as_str())
            .cmp(&(effective_rank(b.kind), b.capability_id.as_str()))
    });

    let mut policy = PolicyDecision {
        exclusions,
        ..PolicyDecision::default()
    };
    policy.selected = viable.first().map(|candidate| (*candidate).clone());
    policy.reason = match (&policy.selected, decision.candidates.is_empty()) {
        (None, true) => PolicyReason::NoCandidates,
        (None, false) => PolicyReason::AllCandidatesExcluded,
        (Some(selected), _) => {
            let default_winner_rank = decision
                .candidates
                .iter()
                .map(|candidate| candidate.kind.rank())
                .min();
            let promoted = preferences.prefer_kind.is_some()
                && Some(selected.kind) == preferences.prefer_kind
                && default_winner_rank != Some(selected.kind.rank());
            if promoted {
                PolicyReason::SelectedByUserPreference
            } else {
                PolicyReason::SelectedByDefaultRank
            }
        }
    };

    // Fallback chain: remaining viable kinds ascending, always closed by
    // HumanHandoff then Reject.
    let mut fallback: Vec<RouteKind> = Vec::new();
    for candidate in &viable {
        let kind = candidate.kind;
        if Some(kind) != policy.selected.as_ref().map(|s| s.kind) && !fallback.contains(&kind) {
            fallback.push(kind);
        }
    }
    for terminal in [RouteKind::HumanHandoff, RouteKind::Reject] {
        if !fallback.contains(&terminal) {
            fallback.push(terminal);
        }
    }
    policy.fallback = fallback;
    Ok(policy)
}

impl PolicyDecision {
    /// Combined deterministic snapshot of the resolution plus this
    /// verdict; only closed tokens and wire names appear.
    #[must_use]
    pub fn snapshot(&self, decision: &RouteDecision) -> String {
        let mut out = String::new();
        out.push_str(&decision.snapshot());
        out.push_str("selected\n");
        match &self.selected {
            Some(candidate) => out.push_str(&format!(
                "{}|{}|{}|{}|{}|{}\n",
                candidate.kind.wire_name(),
                candidate.capability_id,
                candidate.version,
                candidate.trust.wire_name(),
                candidate.data_scope.wire_name(),
                self.reason.wire_name()
            )),
            None => out.push_str(&format!("none|{}\n", self.reason.wire_name())),
        }
        out.push_str("fallback\n");
        for kind in &self.fallback {
            out.push_str(kind.wire_name());
            out.push('\n');
        }
        out.push_str("exclusions\n");
        for exclusion in &self.exclusions {
            out.push_str(&format!(
                "{}|{}\n",
                exclusion.capability_id,
                exclusion.reason.wire_name()
            ));
        }
        out
    }
}

#[cfg(test)]
#[path = "policy_tests.rs"]
mod tests;
