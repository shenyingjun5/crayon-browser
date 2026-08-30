//! Risk policy tests (ACT-06, AC-006): deterministic and monotonic
//! assessment, sensitive-element execution exclusion, page/model requests
//! structurally unable to lower risk, closed reasons and stable wire form.

use crayon_domain::{RiskLevel, RiskReason, SemanticNodeKind};
use crayon_semantic_action::{assess, RiskDecision, RiskFacts, MAX_EXECUTABLE_RISK};

fn clean() -> RiskFacts {
    RiskFacts::default()
}

fn all_facts() -> RiskFacts {
    RiskFacts {
        payment_context: true,
        offsite_navigation: true,
        download_trigger: true,
        cross_origin_frame: true,
        ambiguous_match: true,
        low_confidence: true,
        unverified_effect: true,
    }
}

// ---------- Determinism and monotonicity ----------

#[test]
fn assessment_is_deterministic() {
    let first = assess(SemanticNodeKind::Button, clean());
    let second = assess(SemanticNodeKind::Button, clean());
    assert_eq!(first, second);
    assert_eq!(first.level, RiskLevel::R0);
    assert!(first.reasons.is_empty());
    assert!(first.executable);
}

#[test]
fn every_fact_can_only_raise_the_level() {
    // Each fact in isolation yields its floor level; adding facts never
    // lowers the outcome.
    let isolated = [
        (
            RiskFacts {
                payment_context: true,
                ..RiskFacts::default()
            },
            RiskLevel::R4,
        ),
        (
            RiskFacts {
                offsite_navigation: true,
                ..RiskFacts::default()
            },
            RiskLevel::R3,
        ),
        (
            RiskFacts {
                download_trigger: true,
                ..RiskFacts::default()
            },
            RiskLevel::R3,
        ),
        (
            RiskFacts {
                cross_origin_frame: true,
                ..RiskFacts::default()
            },
            RiskLevel::R3,
        ),
        (
            RiskFacts {
                ambiguous_match: true,
                ..RiskFacts::default()
            },
            RiskLevel::R2,
        ),
        (
            RiskFacts {
                low_confidence: true,
                ..RiskFacts::default()
            },
            RiskLevel::R2,
        ),
        (
            RiskFacts {
                unverified_effect: true,
                ..RiskFacts::default()
            },
            RiskLevel::R2,
        ),
    ];
    for (facts, expected) in isolated {
        let decision = assess(SemanticNodeKind::Button, facts);
        assert_eq!(decision.level, expected, "floor level for {facts:?}");
        assert!(decision.executable || expected > MAX_EXECUTABLE_RISK);
    }
    // Monotone: a superset of facts never yields a lower level.
    let base = assess(
        SemanticNodeKind::Button,
        RiskFacts {
            low_confidence: true,
            ..RiskFacts::default()
        },
    );
    let raised = assess(
        SemanticNodeKind::Button,
        RiskFacts {
            low_confidence: true,
            payment_context: true,
            ..RiskFacts::default()
        },
    );
    assert!(raised.level > base.level);
    assert!(raised.reasons.contains(&RiskReason::LowConfidence));
    assert!(raised.reasons.contains(&RiskReason::PaymentContext));
}

#[test]
fn reasons_are_sorted_deduplicated_and_bounded() {
    let decision = assess(SemanticNodeKind::Button, all_facts());
    assert_eq!(decision.level, RiskLevel::R4);
    assert!(decision.denied());
    assert_eq!(decision.reasons.len(), 7);
    let mut sorted = decision.reasons.clone();
    sorted.sort();
    assert_eq!(decision.reasons, sorted);
    let wire = serde_json::to_string(&decision).expect("serialize");
    assert!(wire.contains("\"r4\""));
    assert!(wire.contains("\"payment_context\""));
}

// ---------- Sensitive-element exclusion ----------

#[test]
fn sensitive_elements_are_never_executable() {
    let password = assess(SemanticNodeKind::PasswordInput, clean());
    assert_eq!(password.level, RiskLevel::R4);
    assert_eq!(password.reasons, vec![RiskReason::SensitiveCredential]);
    assert!(password.denied());

    let file = assess(SemanticNodeKind::FileInput, clean());
    assert_eq!(file.level, RiskLevel::R4);
    assert_eq!(file.reasons, vec![RiskReason::FileUpload]);
    assert!(file.denied());

    // Even a fact-free assessment denies; no context can rehabilitate a
    // sensitive surface.
    assert!(assess(SemanticNodeKind::FileInput, all_facts()).denied());
}

#[test]
fn r3_and_r4_decisions_deny_but_r2_executes() {
    let r3 = assess(
        SemanticNodeKind::Button,
        RiskFacts {
            download_trigger: true,
            ..RiskFacts::default()
        },
    );
    assert_eq!(r3.level, RiskLevel::R3);
    assert!(r3.denied());
    let r2 = assess(
        SemanticNodeKind::Button,
        RiskFacts {
            ambiguous_match: true,
            ..RiskFacts::default()
        },
    );
    assert_eq!(r2.level, RiskLevel::R2);
    assert!(r2.executable);
    assert_eq!(MAX_EXECUTABLE_RISK, RiskLevel::R2);
}

// ---------- No lowering path ----------

#[test]
fn wire_form_has_no_lowering_surface() {
    // The decision struct exposes only level/reasons/executable; a hostile
    // caller cannot inject "lower my risk" — there is no field for it.
    let raw = r#"{"level":"r0","reasons":[],"executable":false,"requested_by_page":true}"#;
    let parsed: Result<RiskDecision, _> = serde_json::from_str(raw);
    assert!(parsed.is_err(), "unknown fields must be rejected");
    // Re-assessing the same facts after a "request" returns identical output.
    let before = assess(SemanticNodeKind::TextInput, clean());
    let after = assess(SemanticNodeKind::TextInput, clean());
    assert_eq!(before, after);
}
