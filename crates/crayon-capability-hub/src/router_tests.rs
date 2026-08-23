//! HUB-03 router contract tests: HB-003 deterministic re-evaluation,
//! complete closed reasons and a secret-free snapshot surface.

use super::*;
use crate::builtin::builtin_registry;
use crate::registry::CapabilityRegistry;
use crayon_domain::{CapabilityDescriptor, CapabilitySource, DataScope, TrustLevel};

fn extra_descriptor(
    id: &str,
    version: &str,
    source: CapabilitySource,
    trust: TrustLevel,
    summary: &str,
) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: version.to_owned(),
        source,
        trust,
        data_scope: DataScope::LocalOnly,
        summary: summary.to_owned(),
    }
}

/// Fixed scenario exercising every evaluation outcome.
fn scenario_registry() -> CapabilityRegistry {
    let mut registry = builtin_registry();
    registry
        .register(extra_descriptor(
            "skill.export",
            "1.0.0",
            CapabilitySource::PersonalSkill,
            TrustLevel::UserApproved,
            "export helper",
        ))
        .expect("personal registers");
    registry
        .register(extra_descriptor(
            "partner.notes",
            "0.9.0",
            CapabilitySource::Partner,
            TrustLevel::Untrusted,
            "notes connector",
        ))
        .expect("partner registers");
    registry
        .set_enabled("builtin.cast", "1.0.0", false)
        .expect("disable");
    registry
}

const SCENARIO_INPUT: [&str; 5] = [
    "skill.export",
    "builtin.content",
    "missing.cap",
    "builtin.cast",
    "builtin.handoff",
];

fn golden_path() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests")
        .join("router_decision_v1.txt")
}

#[test]
fn decision_snapshot_matches_frozen_golden() {
    let decision = resolve(
        &RouteInput::new(&SCENARIO_INPUT).expect("valid"),
        &scenario_registry(),
    );
    let actual = decision.snapshot();
    let golden = std::fs::read_to_string(golden_path()).expect("router golden must exist");
    assert_eq!(actual, golden);
}

/// HB-003 core property: the same RouteInput evaluated repeatedly — even
/// against a rebuilt identical registry — is byte-identical and equal as
/// a value.
#[test]
fn repeated_evaluation_is_stable() {
    let input = RouteInput::new(&SCENARIO_INPUT).expect("valid");
    let first = resolve(&input, &scenario_registry());
    let second = resolve(&input, &scenario_registry());
    assert_eq!(first, second);
    assert_eq!(first.snapshot(), second.snapshot());
    // Re-evaluating the SAME decision object also stays stable.
    assert_eq!(first.snapshot(), first.snapshot());
}

#[test]
fn every_outcome_is_reachable_and_explained() {
    let mut registry = scenario_registry();
    registry.revoke("partner.notes", "0.9.0").expect("revoke");
    let input = RouteInput::new(&[
        "builtin.browser",
        "missing.cap",
        "builtin.cast",
        "partner.notes",
    ])
    .expect("valid");
    let decision = resolve(&input, &registry);
    let outcomes: Vec<(&str, RouteOutcome)> = decision
        .evaluations
        .iter()
        .map(|e| (e.capability_id.as_str(), e.outcome))
        .collect();
    assert_eq!(
        outcomes,
        vec![
            ("builtin.browser", RouteOutcome::Resolved),
            ("missing.cap", RouteOutcome::UnknownId),
            ("builtin.cast", RouteOutcome::Disabled),
            ("partner.notes", RouteOutcome::Revoked),
        ]
    );
    // Only the live registration becomes a candidate, carrying its
    // derived kind and trust.
    assert_eq!(decision.candidates.len(), 1);
    let candidate = &decision.candidates[0];
    assert_eq!(candidate.capability_id, "builtin.browser");
    assert_eq!(candidate.kind, RouteKind::WebAutomation);
    assert_eq!(candidate.trust, TrustLevel::System);
    // Every Resolved evaluation carries its candidate; nothing else does.
    for evaluation in &decision.evaluations {
        assert_eq!(
            evaluation.candidate.is_some(),
            evaluation.outcome == RouteOutcome::Resolved,
            "{} must match candidate presence",
            evaluation.capability_id
        );
    }
}

#[test]
fn candidate_order_is_independent_of_input_order() {
    let forward =
        RouteInput::new(&["builtin.content", "skill.export", "builtin.handoff"]).expect("valid");
    let reversed =
        RouteInput::new(&["builtin.handoff", "skill.export", "builtin.content"]).expect("valid");
    let registry = scenario_registry();
    let mut a = resolve(&forward, &registry).candidates;
    let b = resolve(&reversed, &registry).candidates;
    assert_eq!(a, b);
    // And the order is exactly (kind rank, id).
    a.sort_by(|x, y| {
        (x.kind.rank(), x.capability_id.as_str()).cmp(&(y.kind.rank(), y.capability_id.as_str()))
    });
    assert_eq!(b, a);
    assert_eq!(b[0].kind, RouteKind::SiteSkill);
    assert!(b[1].capability_id < b[2].capability_id);
}

#[test]
fn route_kind_derivation_is_closed() {
    assert_eq!(
        route_kind_of_source(CapabilitySource::Partner),
        RouteKind::Partner
    );
    assert_eq!(
        route_kind_of_source(CapabilitySource::PersonalSkill),
        RouteKind::SiteSkill
    );
    assert_eq!(
        route_kind_of_source(CapabilitySource::Builtin),
        RouteKind::WebAutomation
    );
    // Default policy rank is frozen on the declaration order.
    assert!(RouteKind::Partner.rank() < RouteKind::SiteSkill.rank());
    assert!(RouteKind::SiteSkill.rank() < RouteKind::WebAutomation.rank());
    assert!(RouteKind::WebAutomation.rank() < RouteKind::HumanHandoff.rank());
    assert!(RouteKind::HumanHandoff.rank() < RouteKind::Reject.rank());
}

#[test]
fn route_input_validation_matrix() {
    assert_eq!(
        RouteInput::new(&["Bad Id"]),
        Err(RouterError::InvalidCapabilityId)
    );
    assert_eq!(
        RouteInput::new(&[""]),
        Err(RouterError::InvalidCapabilityId)
    );
    let overlong = "a".repeat(crayon_domain::MAX_CAPABILITY_ID_LEN + 1);
    assert_eq!(
        RouteInput::new(&[&overlong]),
        Err(RouterError::InvalidCapabilityId)
    );
    let max_ids: Vec<String> = (0..MAX_ROUTE_INPUT_IDS)
        .map(|i| format!("cap.{i}"))
        .collect();
    let max_refs: Vec<&str> = max_ids.iter().map(String::as_str).collect();
    assert!(RouteInput::new(&max_refs).is_ok());
    let over_max: Vec<String> = (0..=MAX_ROUTE_INPUT_IDS)
        .map(|i| format!("cap.{i}"))
        .collect();
    let over_refs: Vec<&str> = over_max.iter().map(String::as_str).collect();
    assert_eq!(RouteInput::new(&over_refs), Err(RouterError::TooManyIds));
    assert_eq!(
        RouteInput::new(&["cap.a", "cap.a"]),
        Err(RouterError::DuplicateId)
    );
}

#[test]
fn snapshot_never_carries_free_text() {
    let mut registry = builtin_registry();
    registry
        .register(extra_descriptor(
            "skill.marker",
            "1.0.0",
            CapabilitySource::PersonalSkill,
            TrustLevel::UserApproved,
            "SECRET-SUMMARY-MARKER https://internal.endpoint.example/upload token=abc123",
        ))
        .expect("registers");
    let decision = resolve(
        &RouteInput::new(&["skill.marker"]).expect("valid"),
        &registry,
    );
    let snapshot = decision.snapshot();
    assert!(!snapshot.contains("SECRET-SUMMARY-MARKER"));
    assert!(!snapshot.contains("internal.endpoint"));
    assert!(!snapshot.contains("token="));
    assert!(snapshot.contains("skill.marker|resolved"));
}

/// Deterministic pseudo-random sequence (LCG): same input always yields
/// the same decision, candidates stay rank-sorted, and evaluation count
/// always equals the input count.
#[test]
fn lcg_resolution_invariants() {
    const POOL: [&str; 6] = [
        "builtin.browser",
        "builtin.content",
        "builtin.cast",
        "skill.export",
        "partner.notes",
        "missing.cap",
    ];
    let mut state: u64 = 0x9E37_79B9_7F4A_7C15;
    let mut next = || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };
    let mut registry = scenario_registry();
    for step in 0..3_000_u64 {
        if step % 97 == 0 {
            // Flip availability so determinism is checked across states.
            if next() % 2 == 0 {
                let _ = registry.revoke("skill.export", "1.0.0");
                let _ = registry.register(extra_descriptor(
                    "skill.export",
                    "1.0.1",
                    CapabilitySource::PersonalSkill,
                    TrustLevel::UserApproved,
                    "export helper",
                ));
            } else {
                let _ = registry.set_enabled("builtin.cast", "1.0.0", true);
            }
        }
        let len = (next() % (POOL.len() as u64)) as usize;
        let mut picked = Vec::new();
        let mut seen = std::collections::BTreeSet::new();
        for _ in 0..len {
            let id = POOL[(next() % POOL.len() as u64) as usize];
            if seen.insert(id) {
                picked.push(id);
            }
        }
        if picked.is_empty() {
            continue;
        }
        let input = RouteInput::new(&picked).expect("pool ids are valid tokens");
        let first = resolve(&input, &registry);
        let second = resolve(&input, &registry);
        assert_eq!(first, second, "same input must be stable");
        assert_eq!(first.evaluations.len(), picked.len());
        for pair in first.candidates.windows(2) {
            assert!(
                (pair[0].kind.rank(), pair[0].capability_id.as_str())
                    < (pair[1].kind.rank(), pair[1].capability_id.as_str()),
                "candidates must stay strictly ordered"
            );
        }
        let resolved = first
            .evaluations
            .iter()
            .filter(|e| e.outcome == RouteOutcome::Resolved)
            .count();
        assert_eq!(first.candidates.len(), resolved);
        assert!(first.candidates.len() <= picked.len());
    }
}
