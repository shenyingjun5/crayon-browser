//! HUB-04 policy tests: HB-004 default priority and override matrix,
//! trust/data-scope gates and deterministic fallback ordering.

use super::*;
use crate::builtin::builtin_registry;
use crate::registry::CapabilityRegistry;
use crate::router::{resolve, RouteDecision, RouteInput};
use crayon_domain::{CapabilityDescriptor, CapabilitySource};

const ALL_IDS: [&str; 5] = [
    "builtin.content",
    "builtin.handoff",
    "skill.export",
    "partner.notes",
    "partner.approved",
];

fn descriptor(
    id: &str,
    version: &str,
    source: CapabilitySource,
    trust: TrustLevel,
    scope: DataScope,
) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: version.to_owned(),
        source,
        trust,
        data_scope: scope,
        summary: format!("{id} {version}"),
    }
}

/// Fixed scenario covering every gate: an untrusted partner, an approved
/// external partner, an approved local skill and two builtin capabilities.
fn policy_registry() -> CapabilityRegistry {
    let mut registry = builtin_registry();
    for (id, version, source, trust, scope) in [
        (
            "partner.notes",
            "0.9.0",
            CapabilitySource::Partner,
            TrustLevel::Untrusted,
            DataScope::ExternalEndpoint,
        ),
        (
            "partner.approved",
            "1.1.0",
            CapabilitySource::Partner,
            TrustLevel::UserApproved,
            DataScope::ExternalEndpoint,
        ),
        (
            "skill.export",
            "1.0.0",
            CapabilitySource::PersonalSkill,
            TrustLevel::UserApproved,
            DataScope::LocalOnly,
        ),
    ] {
        registry
            .register(descriptor(id, version, source, trust, scope))
            .expect("scenario registers");
    }
    registry
}

fn scenario_decision() -> RouteDecision {
    resolve(
        &RouteInput::new(&ALL_IDS).expect("valid"),
        &policy_registry(),
    )
}

#[test]
fn default_rank_prefers_approved_partner() {
    let decision = scenario_decision();
    let policy = apply(&PolicyPreferences::default_policy(), &decision).expect("valid prefs");
    let selected = policy.selected.as_ref().expect("approved partner wins");
    assert_eq!(selected.capability_id, "partner.approved");
    assert_eq!(selected.kind, RouteKind::Partner);
    assert_eq!(policy.reason, PolicyReason::SelectedByDefaultRank);
    // The untrusted twin is excluded even under the default policy.
    assert_eq!(
        policy.exclusions,
        vec![Exclusion {
            capability_id: "partner.notes".to_owned(),
            reason: ExclusionReason::InsufficientTrust,
        }]
    );
    // Fallback: remaining viable kinds ascending, terminal pair enforced.
    assert_eq!(
        policy.fallback,
        vec![
            RouteKind::SiteSkill,
            RouteKind::WebAutomation,
            RouteKind::HumanHandoff,
            RouteKind::Reject,
        ]
    );
}

#[test]
fn untrusted_candidates_are_never_selected() {
    // Only the untrusted partner is proposed: nothing else may win.
    let input = RouteInput::new(&["partner.notes"]).expect("valid");
    let decision = resolve(&input, &policy_registry());
    let policy = apply(&PolicyPreferences::default_policy(), &decision).expect("valid prefs");
    assert!(policy.selected.is_none());
    assert_eq!(policy.reason, PolicyReason::AllCandidatesExcluded);
    assert_eq!(
        policy.fallback,
        vec![RouteKind::HumanHandoff, RouteKind::Reject]
    );
}

/// The data-exfiltration constraint removes external-endpoint scopes even
/// when they are trusted and top-ranked.
#[test]
fn external_endpoint_constraint_matrix() {
    // Constraint on: both partners drop out, the local skill wins.
    let prefs = PolicyPreferences {
        prefer_kind: None,
        allow_external_endpoint: false,
    };
    let decision = scenario_decision();
    let policy = apply(&prefs, &decision).expect("valid prefs");
    let selected = policy.selected.as_ref().expect("local skill wins");
    assert_eq!(selected.capability_id, "skill.export");
    assert_eq!(
        policy.exclusions,
        vec![
            Exclusion {
                capability_id: "partner.approved".to_owned(),
                reason: ExclusionReason::ExternalDataForbidden,
            },
            // The trust gate is checked first, so the untrusted twin keeps
            // its own reason even though both gates would hit.
            Exclusion {
                capability_id: "partner.notes".to_owned(),
                reason: ExclusionReason::InsufficientTrust,
            },
        ]
    );
    // Constraint off (default): the trusted external partner stays viable —
    // covered by default_rank_prefers_approved_partner.
}

#[test]
fn user_preference_promotes_a_kind_without_breaking_order() {
    let prefs = PolicyPreferences {
        prefer_kind: Some(RouteKind::WebAutomation),
        allow_external_endpoint: true,
    };
    let decision = scenario_decision();
    let policy = apply(&prefs, &decision).expect("valid prefs");
    let selected = policy.selected.as_ref().expect("promoted web wins");
    assert_eq!(selected.kind, RouteKind::WebAutomation);
    assert_eq!(selected.capability_id, "builtin.content");
    assert_eq!(policy.reason, PolicyReason::SelectedByUserPreference);
    // Remaining kinds keep the frozen relative order behind the winner.
    assert_eq!(
        policy.fallback,
        vec![
            RouteKind::Partner,
            RouteKind::SiteSkill,
            RouteKind::HumanHandoff,
            RouteKind::Reject,
        ]
    );
}

#[test]
fn reject_is_not_a_valid_preference() {
    let prefs = PolicyPreferences {
        prefer_kind: Some(RouteKind::Reject),
        allow_external_endpoint: true,
    };
    assert_eq!(
        apply(&prefs, &scenario_decision()),
        Err(PolicyError::InvalidPreference)
    );
}

/// Unavailable paths never reach selection: disabled and revoked ids are
/// already dropped by the router, so the policy can only pick live ones.
#[test]
fn unavailable_paths_are_end_to_end_unselectable() {
    let mut registry = policy_registry();
    registry
        .set_enabled("skill.export", "1.0.0", false)
        .expect("disable");
    registry
        .revoke("partner.approved", "1.1.0")
        .expect("revoke");
    let decision = resolve(&RouteInput::new(&ALL_IDS).expect("valid"), &registry);
    let policy = apply(&PolicyPreferences::default_policy(), &decision).expect("valid prefs");
    // The unavailable pair never becomes a candidate at all.
    let candidate_ids: Vec<&str> = decision
        .candidates
        .iter()
        .map(|candidate| candidate.capability_id.as_str())
        .collect();
    assert!(!candidate_ids.contains(&"skill.export"));
    assert!(!candidate_ids.contains(&"partner.approved"));
    // Untrusted partner is gated, so the builtin web path wins.
    assert_eq!(
        policy.selected.as_ref().map(|s| s.capability_id.as_str()),
        Some("builtin.content")
    );
}

#[test]
fn empty_resolution_reports_no_candidates() {
    let decision = resolve(
        &RouteInput::new(&["missing.cap"]).expect("valid"),
        &policy_registry(),
    );
    let policy = apply(&PolicyPreferences::default_policy(), &decision).expect("valid prefs");
    assert!(policy.selected.is_none());
    assert_eq!(policy.reason, PolicyReason::NoCandidates);
    assert_eq!(
        policy.fallback,
        vec![RouteKind::HumanHandoff, RouteKind::Reject]
    );
}

fn golden_path() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests")
        .join("policy_decision_v1.txt")
}

#[test]
fn policy_snapshot_matches_frozen_golden() {
    let decision = scenario_decision();
    let policy = apply(&PolicyPreferences::default_policy(), &decision).expect("valid prefs");
    let actual = policy.snapshot(&decision);
    let golden = std::fs::read_to_string(golden_path()).expect("policy golden must exist");
    assert_eq!(actual, golden);
}

/// Deterministic pseudo-random sequence (LCG): whatever the preferences,
/// a selection always passes both gates, beats every other viable
/// candidate under the effective ranking, and the fallback chain is well
/// formed.
#[test]
fn lcg_policy_invariants() {
    const KINDS_NO_REJECT: [RouteKind; 4] = [
        RouteKind::Partner,
        RouteKind::SiteSkill,
        RouteKind::WebAutomation,
        RouteKind::HumanHandoff,
    ];
    let mut state: u64 = 0x6A09_E667_F3BC_C908;
    let mut next = || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };
    for _ in 0..3_000_u64 {
        let prefer = match next() % 5 {
            0 => None,
            index => Some(KINDS_NO_REJECT[(index - 1) as usize]),
        };
        let prefs = PolicyPreferences {
            prefer_kind: prefer,
            allow_external_endpoint: next() % 2 == 0,
        };
        let len = 1 + (next() % ALL_IDS.len() as u64) as usize;
        let picked = &ALL_IDS[..len];
        let decision = resolve(&RouteInput::new(picked).expect("valid"), &policy_registry());
        let policy = apply(&prefs, &decision).expect("reject never generated");

        // Gate invariants on the selection.
        if let Some(selected) = &policy.selected {
            assert_ne!(selected.trust, TrustLevel::Untrusted);
            if !prefs.allow_external_endpoint {
                assert_ne!(selected.data_scope, DataScope::ExternalEndpoint);
            }
            // No viable candidate may rank before it under the effective rank.
            let selected_effective = match prefs.prefer_kind {
                Some(kind) if kind == selected.kind => 0,
                _ => selected.kind.rank(),
            };
            let excluded_ids: Vec<&str> = policy
                .exclusions
                .iter()
                .map(|exclusion| exclusion.capability_id.as_str())
                .collect();
            for candidate in &decision.candidates {
                if excluded_ids.contains(&candidate.capability_id.as_str()) {
                    continue;
                }
                let effective = match prefs.prefer_kind {
                    Some(kind) if kind == candidate.kind => 0,
                    _ => candidate.kind.rank(),
                };
                assert!(
                    (effective, candidate.capability_id.as_str())
                        >= (selected_effective, selected.capability_id.as_str()),
                    "candidate {} must not beat selected {}",
                    candidate.capability_id,
                    selected.capability_id
                );
            }
        }
        // Chain shape and determinism.
        assert_eq!(policy.fallback.last(), Some(&RouteKind::Reject));
        assert_eq!(
            policy.fallback.get(policy.fallback.len() - 2),
            Some(&RouteKind::HumanHandoff)
        );
        let mut sorted = policy.exclusions.clone();
        sorted.sort_by(|a, b| a.capability_id.cmp(&b.capability_id));
        assert_eq!(policy.exclusions, sorted);
        let replay = apply(&prefs, &decision).expect("stable");
        assert_eq!(policy.snapshot(&decision), replay.snapshot(&decision));
    }
}
