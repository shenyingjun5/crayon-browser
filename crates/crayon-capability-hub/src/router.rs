//! Router stable contracts and deterministic resolution (HUB-03).
//!
//! This module freezes the shapes exchanged with the router: a bounded,
//! validated [`RouteInput`] is resolved against the [`CapabilityRegistry`]
//! into per-id [`RouteEvaluation`]s (complete closed reasons) and live
//! [`RouteCandidate`]s in a deterministic order.  The default policy
//! ordering, trust/health/preference overrides, selection and fallback
//! re-authorization belong to HUB-04/HUB-05.
//!
//! Snapshots carry only closed tokens and enum wire names — never
//! summaries, endpoints or secrets.

use crate::registry::CapabilityRegistry;
use crayon_domain::{is_capability_token, CapabilitySource, LifecycleState};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum number of candidate ids one route input may propose.
pub const MAX_ROUTE_INPUT_IDS: usize = 16;

/// Closed route kinds.  Declaration order IS the frozen default policy
/// rank (`Partner` wins over `SiteSkill`, then `WebAutomation`, then
/// `HumanHandoff`, then `Reject`); HUB-04 owns override rules on top of
/// this fixed basis.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum RouteKind {
    /// Approved partner API/MCP connector packages.
    Partner,
    /// Personal Site Skills saved by the user.
    SiteSkill,
    /// Controlled in-browser automation over builtin capabilities.
    WebAutomation,
    /// Pause and hand the task to the user.
    HumanHandoff,
    /// No viable path; the task does not proceed.
    Reject,
}

impl RouteKind {
    /// Stable wire name used by snapshots.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Partner => "partner",
            Self::SiteSkill => "site_skill",
            Self::WebAutomation => "web_automation",
            Self::HumanHandoff => "human_handoff",
            Self::Reject => "reject",
        }
    }

    /// Rank used for deterministic candidate ordering (identical to the
    /// declaration/default policy order).
    #[must_use]
    pub const fn rank(self) -> u8 {
        self as u8
    }
}

/// Derives the route kind of a registry-backed registration.  Only the
/// three registration-backed kinds are derivable; `HumanHandoff` and
/// `Reject` are policy-level states that can never come from a
/// registration.
#[must_use]
pub const fn route_kind_of_source(source: CapabilitySource) -> RouteKind {
    match source {
        CapabilitySource::Partner => RouteKind::Partner,
        CapabilitySource::PersonalSkill => RouteKind::SiteSkill,
        CapabilitySource::Builtin => RouteKind::WebAutomation,
    }
}

/// One routable option resolved from a live registration.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RouteCandidate {
    pub capability_id: String,
    pub version: String,
    pub kind: RouteKind,
    pub trust: crayon_domain::TrustLevel,
    /// Declared data scope, carried for the policy layer's data-exfiltration
    /// constraint (HUB-04) and route previews (HUB-06).
    pub data_scope: crayon_domain::DataScope,
}

/// Closed per-input evaluation outcome; together the outcomes form the
/// complete, secret-free route reason surface.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RouteOutcome {
    /// Live registration found and proposed as a candidate.
    Resolved,
    /// The id is unknown to the registry.
    UnknownId,
    /// The current registration is disabled.
    Disabled,
    /// The current registration is revoked (terminal).
    Revoked,
}

impl RouteOutcome {
    /// Stable wire name used by snapshots.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Resolved => "resolved",
            Self::UnknownId => "unknown_id",
            Self::Disabled => "disabled",
            Self::Revoked => "revoked",
        }
    }
}

/// One input id's evaluation, in input order.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RouteEvaluation {
    pub capability_id: String,
    pub outcome: RouteOutcome,
    /// Present exactly when `outcome` is `Resolved`.
    pub candidate: Option<RouteCandidate>,
}

/// What a caller proposes for routing.  The proposal is untrusted data:
/// ids are validated and can only ever resolve through the registry.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RouteInput {
    pub capability_ids: Vec<String>,
}

impl RouteInput {
    /// Validates and builds an input: closed tokens only, bounded count,
    /// no duplicates.
    pub fn new(capability_ids: &[&str]) -> Result<Self, RouterError> {
        if capability_ids.len() > MAX_ROUTE_INPUT_IDS {
            return Err(RouterError::TooManyIds);
        }
        let mut seen = std::collections::BTreeSet::new();
        for id in capability_ids {
            if !is_capability_token(id) {
                return Err(RouterError::InvalidCapabilityId);
            }
            if !seen.insert(*id) {
                return Err(RouterError::DuplicateId);
            }
        }
        Ok(Self {
            capability_ids: capability_ids.iter().map(|id| (*id).to_owned()).collect(),
        })
    }
}

/// Input validation failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RouterError {
    /// An id is empty, overlong or outside the token charset.
    InvalidCapabilityId,
    /// More than `MAX_ROUTE_INPUT_IDS` ids were proposed.
    TooManyIds,
    /// The same id was proposed twice.
    DuplicateId,
}

impl Display for RouterError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidCapabilityId => "route input id violates shape or bounds",
            Self::TooManyIds => "route input exceeds the candidate bound",
            Self::DuplicateId => "route input proposes the same id twice",
        };
        formatter.write_str(message)
    }
}

impl Error for RouterError {}

/// Stable decision shape: complete per-id evaluations plus the live
/// candidates in deterministic `(kind rank, id)` order.  Selection and
/// fallback chains join this shape with the HUB-04 policy layer.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct RouteDecision {
    pub evaluations: Vec<RouteEvaluation>,
    pub candidates: Vec<RouteCandidate>,
}

impl RouteDecision {
    /// Deterministic snapshot: candidate lines then evaluation lines,
    /// all fields closed tokens or enum wire names.
    #[must_use]
    pub fn snapshot(&self) -> String {
        let mut out = String::new();
        out.push_str("candidates\n");
        for candidate in &self.candidates {
            out.push_str(&format!(
                "{}|{}|{}|{}|{}\n",
                candidate.kind.wire_name(),
                candidate.capability_id,
                candidate.version,
                candidate.trust.wire_name(),
                candidate.data_scope.wire_name()
            ));
        }
        out.push_str("evaluations\n");
        for evaluation in &self.evaluations {
            out.push_str(&format!(
                "{}|{}\n",
                evaluation.capability_id,
                evaluation.outcome.wire_name()
            ));
        }
        out
    }
}

/// Deterministically resolves an input against the registry: every
/// proposed id yields exactly one evaluation (in input order); live
/// registrations become candidates sorted by `(kind rank, id)`
/// regardless of input order.  Repeated evaluation of the same input
/// against the same registry state is byte-identical.
#[must_use]
pub fn resolve(input: &RouteInput, registry: &CapabilityRegistry) -> RouteDecision {
    let mut evaluations = Vec::with_capacity(input.capability_ids.len());
    let mut candidates = Vec::new();
    for id in &input.capability_ids {
        let evaluation = match registry.find(id) {
            None => RouteEvaluation {
                capability_id: id.clone(),
                outcome: RouteOutcome::UnknownId,
                candidate: None,
            },
            Some(view) => match view.state {
                LifecycleState::Active => {
                    let candidate = RouteCandidate {
                        capability_id: id.clone(),
                        version: view.descriptor.version.clone(),
                        kind: route_kind_of_source(view.descriptor.source),
                        trust: view.descriptor.trust,
                        data_scope: view.descriptor.data_scope,
                    };
                    candidates.push(candidate.clone());
                    RouteEvaluation {
                        capability_id: id.clone(),
                        outcome: RouteOutcome::Resolved,
                        candidate: Some(candidate),
                    }
                }
                LifecycleState::Disabled => RouteEvaluation {
                    capability_id: id.clone(),
                    outcome: RouteOutcome::Disabled,
                    candidate: None,
                },
                LifecycleState::Revoked => RouteEvaluation {
                    capability_id: id.clone(),
                    outcome: RouteOutcome::Revoked,
                    candidate: None,
                },
            },
        };
        evaluations.push(evaluation);
    }
    candidates.sort_by(|a, b| {
        (a.kind.rank(), a.capability_id.as_str()).cmp(&(b.kind.rank(), b.capability_id.as_str()))
    });
    RouteDecision {
        evaluations,
        candidates,
    }
}

#[cfg(test)]
#[path = "router_tests.rs"]
mod tests;
