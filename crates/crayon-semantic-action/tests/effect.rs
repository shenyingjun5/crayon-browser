//! Effect verification tests (ACT-08, AC-008): only verified reports
//! success, bounded waits fail closed, repeated idempotency keys freeze the
//! prior terminal report, and indeterminate outcomes block replay.

use crayon_domain::{
    ActionKind, EffectOutcome, EffectReason, EffectReport, SemanticSchemaError, SessionGeneration,
    TabId,
};
use crayon_semantic_action::{
    CheckOutcome, EffectLedger, EffectWaitSpec, IdempotencyKey, IdempotencyKeyError,
    MAX_EFFECT_LEDGER, MAX_EFFECT_WAIT_MS,
};

fn node_id(raw: &str) -> crayon_domain::SemanticNodeId {
    crayon_domain::SemanticNodeId::new(raw).expect("valid node id")
}

fn report(outcome: EffectOutcome, reason: Option<EffectReason>) -> EffectReport {
    EffectReport::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        7,
        ActionKind::Click,
        node_id("n-1"),
        outcome,
        reason,
        None,
    )
    .expect("valid report")
}

fn key(raw: &str) -> IdempotencyKey {
    IdempotencyKey::new(raw).expect("valid key")
}

// ---------- Bounded waits ----------

#[test]
fn waits_are_bounded_and_fail_closed() {
    assert!(EffectWaitSpec::new(1_000, 2_000).is_ok());
    assert_eq!(
        EffectWaitSpec::new(1_000, 1_000),
        Err(SemanticSchemaError::BudgetExceeded("effect wait"))
    );
    assert_eq!(
        EffectWaitSpec::new(1_000, 1_001 + MAX_EFFECT_WAIT_MS),
        Err(SemanticSchemaError::BudgetExceeded("effect wait"))
    );
    let wait = EffectWaitSpec::new(1_000, 2_000).expect("valid wait");
    assert!(!wait.elapsed_at(1_999));
    assert!(wait.elapsed_at(2_000));
}

// ---------- Idempotency ----------

#[test]
fn repeated_keys_freeze_the_prior_report_without_reexecution() {
    let mut ledger = EffectLedger::new();
    let k = key("req.0001");
    assert_eq!(ledger.check(&k), CheckOutcome::Fresh);
    ledger
        .record(k.clone(), report(EffectOutcome::Verified, None))
        .expect("record");
    // A transport retry of the same key sees the frozen report.
    match ledger.check(&k) {
        CheckOutcome::AlreadyReported(report) => {
            assert_eq!(report.outcome, EffectOutcome::Verified);
        }
        other => panic!("unexpected check outcome: {other:?}"),
    }
    // Recording again is rejected; the frozen record cannot be rewritten.
    assert_eq!(
        ledger.record(
            k.clone(),
            report(EffectOutcome::Failed, Some(EffectReason::Unknown))
        ),
        Err(SemanticSchemaError::DuplicateEntry("effect record"))
    );
}

#[test]
fn indeterminate_blocks_replay_but_failed_is_reportable() {
    let mut ledger = EffectLedger::new();
    let k = key("req.0002");
    ledger
        .record(
            k.clone(),
            report(EffectOutcome::Indeterminate, Some(EffectReason::Timeout)),
        )
        .expect("record");
    assert_eq!(ledger.check(&k), CheckOutcome::IndeterminateBlocked);
    // An indeterminate record can never be replaced by a later verified one.
    assert_eq!(
        ledger.record(k.clone(), report(EffectOutcome::Verified, None)),
        Err(SemanticSchemaError::DuplicateEntry("effect record"))
    );
    // A failed outcome is terminal but reportable: a retry of that key
    // returns the frozen failure without re-running the action.
    let k2 = key("req.0003");
    ledger
        .record(
            k2.clone(),
            report(EffectOutcome::Failed, Some(EffectReason::Unknown)),
        )
        .expect("record");
    match ledger.check(&k2) {
        CheckOutcome::AlreadyReported(report) => {
            assert_eq!(report.outcome, EffectOutcome::Failed);
        }
        other => panic!("unexpected check outcome: {other:?}"),
    }
}

#[test]
fn only_verified_counts_as_success() {
    assert!(EffectLedger::is_success(EffectOutcome::Verified));
    assert!(!EffectLedger::is_success(EffectOutcome::Failed));
    assert!(!EffectLedger::is_success(EffectOutcome::Indeterminate));
}

// ---------- Key validity and ledger bounds ----------

#[test]
fn keys_are_closed_tokens_and_handle_derived() {
    assert_eq!(IdempotencyKey::new(""), Err(IdempotencyKeyError::Empty));
    assert!(matches!(
        IdempotencyKey::new("UPPER"),
        Err(IdempotencyKeyError::InvalidCharset)
    ));
    assert!(matches!(
        IdempotencyKey::new(&"a".repeat(65)),
        Err(IdempotencyKeyError::TooLong)
    ));
    assert!(IdempotencyKey::new("req.0001:h-a1b2c3").is_ok());
    let handle = crayon_semantic_action::ActionHandleId::generate().expect("entropy");
    let derived = IdempotencyKey::for_handle(&handle);
    assert_eq!(derived.as_str(), format!("h.{}", handle.as_str()));
    // Same handle always derives the same key.
    assert_eq!(IdempotencyKey::for_handle(&handle), derived);
}

#[test]
fn ledger_is_bounded() {
    let mut ledger = EffectLedger::new();
    for index in 0..MAX_EFFECT_LEDGER {
        ledger
            .record(
                key(&format!("req.{index:06}")),
                report(EffectOutcome::Verified, None),
            )
            .expect("record fits");
    }
    assert_eq!(ledger.len(), MAX_EFFECT_LEDGER);
    assert_eq!(
        ledger.record(key("req.overflow"), report(EffectOutcome::Verified, None)),
        Err(SemanticSchemaError::BudgetExceeded("effect ledger"))
    );
}

// ---------- Wire form ----------

#[test]
fn keys_roundtrip_through_the_wire_form() {
    let k = key("req.0001");
    let wire = serde_json::to_string(&k).expect("serialize");
    assert_eq!(wire, "\"req.0001\"");
    let parsed: IdempotencyKey = serde_json::from_str(&wire).expect("deserialize");
    assert_eq!(parsed, k);
}
