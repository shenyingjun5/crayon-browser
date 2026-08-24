//! HUB-05 fallback verdict tests: HB-005 side-effect branches, fresh
//! authorization on every step and deterministic rendering.

use super::*;
use crate::policy::{apply, PolicyPreferences, PolicyReason};
use crate::registry::CapabilityRegistry;
use crate::router::{resolve, RouteCandidate, RouteInput};
use crayon_domain::{CapabilitySource, DataScope, TrustLevel};

/// Builds a decision whose selected route is `executed` with the given
/// remaining fallback chain (mirroring what the policy would emit).
fn decision_for(executed: RouteKind, fallback: Vec<RouteKind>) -> PolicyDecision {
    let candidate = |kind: RouteKind| RouteCandidate {
        capability_id: format!("cap.{}", kind.wire_name()),
        version: "1.0.0".to_owned(),
        kind,
        trust: TrustLevel::UserApproved,
        data_scope: DataScope::LocalOnly,
    };
    PolicyDecision {
        selected: Some(candidate(executed)),
        fallback,
        reason: PolicyReason::SelectedByDefaultRank,
        exclusions: Vec::new(),
    }
}

fn attempt(kind: RouteKind, side_effects: SideEffectState) -> RouteAttempt {
    RouteAttempt {
        executed_kind: kind,
        side_effects,
    }
}

#[test]
fn clean_failure_falls_to_next_capability_with_full_checklist() {
    let decision = decision_for(
        RouteKind::Partner,
        vec![
            RouteKind::SiteSkill,
            RouteKind::WebAutomation,
            RouteKind::HumanHandoff,
            RouteKind::Reject,
        ],
    );
    let verdict = evaluate(
        &decision,
        &attempt(RouteKind::Partner, SideEffectState::None),
    )
    .expect("valid attempt");
    assert_eq!(
        verdict,
        FallbackVerdict::Reauthorize {
            next: RouteKind::SiteSkill
        }
    );
    // The checklist is always complete — no item may be waived.
    assert_eq!(REAUTHORIZATION_CHECKLIST.len(), 6);
    assert!(REAUTHORIZATION_CHECKLIST.contains(&"idempotency_key"));
    assert_eq!(verdict.snapshot_line(), "reauthorize|site_skill");
}

#[test]
fn committed_reversible_effects_still_require_fresh_authorization() {
    let decision = decision_for(
        RouteKind::Partner,
        vec![
            RouteKind::WebAutomation,
            RouteKind::HumanHandoff,
            RouteKind::Reject,
        ],
    );
    let verdict = evaluate(
        &decision,
        &attempt(
            RouteKind::Partner,
            SideEffectState::Committed { reversible: true },
        ),
    )
    .expect("valid attempt");
    // Even a "safe" commit never replays silently onto the next provider:
    // the verdict is still reauthorize-with-checklist, not auto-retry.
    assert_eq!(
        verdict,
        FallbackVerdict::Reauthorize {
            next: RouteKind::WebAutomation
        }
    );
}

#[test]
fn irreversible_commits_stop_outright() {
    let decision = decision_for(
        RouteKind::Partner,
        vec![
            RouteKind::SiteSkill,
            RouteKind::HumanHandoff,
            RouteKind::Reject,
        ],
    );
    let verdict = evaluate(
        &decision,
        &attempt(
            RouteKind::Partner,
            SideEffectState::Committed { reversible: false },
        ),
    )
    .expect("valid attempt");
    assert_eq!(
        verdict,
        FallbackVerdict::Stop {
            reason: StopReason::IrreversibleCommit
        }
    );
}

/// Unknown side effects preempt the chain regardless of what remains.
#[test]
fn unknown_side_effects_stop_before_any_fallback() {
    let decision = decision_for(
        RouteKind::WebAutomation,
        vec![RouteKind::HumanHandoff, RouteKind::Reject],
    );
    let verdict = evaluate(
        &decision,
        &attempt(RouteKind::WebAutomation, SideEffectState::Unknown),
    )
    .expect("valid attempt");
    assert_eq!(
        verdict,
        FallbackVerdict::Stop {
            reason: StopReason::UnknownSideEffects
        }
    );
    assert_eq!(verdict.snapshot_line(), "stop|unknown_side_effects");
}

#[test]
fn human_handoff_next_yields_hand_over() {
    let decision = decision_for(
        RouteKind::WebAutomation,
        vec![RouteKind::HumanHandoff, RouteKind::Reject],
    );
    let verdict = evaluate(
        &decision,
        &attempt(RouteKind::WebAutomation, SideEffectState::None),
    )
    .expect("valid attempt");
    assert_eq!(verdict, FallbackVerdict::HandOver);
    assert_eq!(verdict.snapshot_line(), "hand_over");
}

#[test]
fn exhausted_chain_stops() {
    // Reject terminal directly.
    let decision = decision_for(RouteKind::WebAutomation, vec![RouteKind::Reject]);
    assert_eq!(
        evaluate(
            &decision,
            &attempt(RouteKind::WebAutomation, SideEffectState::None)
        ),
        Ok(FallbackVerdict::Stop {
            reason: StopReason::ChainExhausted
        })
    );
    // Empty chain.
    let decision = decision_for(RouteKind::WebAutomation, vec![]);
    assert_eq!(
        evaluate(
            &decision,
            &attempt(RouteKind::WebAutomation, SideEffectState::None)
        ),
        Ok(FallbackVerdict::Stop {
            reason: StopReason::ChainExhausted
        })
    );
    assert_eq!(
        FallbackVerdict::Stop {
            reason: StopReason::ChainExhausted
        }
        .snapshot_line(),
        "stop|chain_exhausted"
    );
}

#[test]
fn mismatched_attempt_kind_is_rejected() {
    let decision = decision_for(
        RouteKind::Partner,
        vec![
            RouteKind::SiteSkill,
            RouteKind::HumanHandoff,
            RouteKind::Reject,
        ],
    );
    assert_eq!(
        evaluate(
            &decision,
            &attempt(RouteKind::WebAutomation, SideEffectState::None)
        ),
        Err(FallbackError::RouteNotSelected)
    );
    // A decision with nothing selected cannot be evaluated at all.
    let unselected = PolicyDecision {
        selected: None,
        fallback: vec![RouteKind::HumanHandoff],
        reason: PolicyReason::AllCandidatesExcluded,
        exclusions: Vec::new(),
    };
    assert_eq!(
        evaluate(
            &unselected,
            &attempt(RouteKind::Partner, SideEffectState::None)
        ),
        Err(FallbackError::RouteNotSelected)
    );
}

/// End-to-end shape check against the real policy output: partner fails →
/// site skill is next; the checklist renders fully.
#[test]
fn end_to_end_with_policy_decision_is_deterministic() {
    let mut registry = CapabilityRegistry::new();
    for (id, source, trust) in [
        ("p.a", CapabilitySource::Partner, TrustLevel::UserApproved),
        (
            "s.b",
            CapabilitySource::PersonalSkill,
            TrustLevel::UserApproved,
        ),
        ("b.c", CapabilitySource::Builtin, TrustLevel::System),
    ] {
        registry
            .register(crayon_domain::CapabilityDescriptor {
                id: id.to_owned(),
                version: "1.0.0".to_owned(),
                source,
                trust,
                data_scope: DataScope::LocalOnly,
                summary: String::new(),
            })
            .expect("registers");
    }
    let decision = resolve(
        &RouteInput::new(&["p.a", "s.b", "b.c"]).expect("valid"),
        &registry,
    );
    let policy = apply(&PolicyPreferences::default_policy(), &decision).expect("policy");
    let verdict = evaluate(
        &policy,
        &attempt(
            policy.selected.as_ref().expect("selected").kind,
            SideEffectState::None,
        ),
    )
    .expect("verdict");
    assert_eq!(verdict.snapshot_line(), "reauthorize|site_skill");
}

/// Deterministic pseudo-random sequence (LCG): whatever the chain, a
/// Reauthorize target is always a capability kind present in the chain,
/// safety stops always win over chain position, and rendering stays
/// closed.
#[test]
fn lcg_verdict_invariants() {
    const CAPABILITY_KINDS: [RouteKind; 3] = [
        RouteKind::Partner,
        RouteKind::SiteSkill,
        RouteKind::WebAutomation,
    ];
    let mut state: u64 = 0x9E37_79B9_7F4A_7C15;
    let mut next = || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };
    for _ in 0..3_000_u64 {
        let executed = CAPABILITY_KINDS[(next() % 3) as usize];
        let chain_len = (next() % 5) as usize;
        let mut fallback = Vec::new();
        for _ in 0..chain_len {
            let pick = next() % 5;
            fallback.push(match pick {
                0 => RouteKind::Partner,
                1 => RouteKind::SiteSkill,
                2 => RouteKind::WebAutomation,
                3 => RouteKind::HumanHandoff,
                _ => RouteKind::Reject,
            });
        }
        let side_effects = match next() % 4 {
            0 => SideEffectState::None,
            1 => SideEffectState::Committed { reversible: true },
            2 => SideEffectState::Committed { reversible: false },
            _ => SideEffectState::Unknown,
        };
        let decision = decision_for(executed, fallback.clone());
        let verdict = evaluate(&decision, &attempt(executed, side_effects));
        match side_effects {
            SideEffectState::Unknown => assert_eq!(
                verdict,
                Ok(FallbackVerdict::Stop {
                    reason: StopReason::UnknownSideEffects
                })
            ),
            SideEffectState::Committed { reversible: false } => assert_eq!(
                verdict,
                Ok(FallbackVerdict::Stop {
                    reason: StopReason::IrreversibleCommit
                })
            ),
            _ => match verdict.expect("safe branch yields a verdict") {
                FallbackVerdict::Reauthorize { next } => {
                    assert!(fallback.contains(&next));
                    assert!(CAPABILITY_KINDS.contains(&next));
                    assert_eq!(Some(&next), fallback.first());
                }
                FallbackVerdict::HandOver => {
                    assert_eq!(fallback.first(), Some(&RouteKind::HumanHandoff));
                }
                FallbackVerdict::Stop { reason } => {
                    assert_eq!(reason, StopReason::ChainExhausted);
                }
            },
        }
    }
}
