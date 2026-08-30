//! Handoff tests (ACT-11, AC-011): closed reasons with frozen kinds,
//! recoverable handoffs demand fresh read and fresh confirmation, no
//! permission inheritance, and no implicit retry surface.

use crayon_domain::{ActionKind, SemanticNodeId, SessionGeneration, TabId};
use crayon_semantic_action::{handoff, HandoffKind, HandoffReason, HandoffRecord};

fn node_id(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn record(reason: HandoffReason) -> HandoffRecord {
    handoff(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        node_id("n-1"),
        ActionKind::Click,
        reason,
    )
}

#[test]
fn reasons_are_closed_with_frozen_kinds() {
    assert_eq!(HandoffReason::ALL.len(), 9);
    for reason in HandoffReason::ALL {
        let expected = matches!(
            reason,
            HandoffReason::UserTakeoverRequested
                | HandoffReason::ChallengeDetected
                | HandoffReason::ConfirmationExpired
                | HandoffReason::ExecutionInterrupted
                | HandoffReason::DeadlineElapsed
        );
        assert_eq!(
            reason.kind(),
            if expected {
                HandoffKind::Recoverable
            } else {
                HandoffKind::Unrecoverable
            },
            "frozen kind for {reason:?}"
        );
    }
}

#[test]
fn recoverable_handoffs_demand_fresh_read_and_fresh_confirmation() {
    for reason in [
        HandoffReason::UserTakeoverRequested,
        HandoffReason::ChallengeDetected,
        HandoffReason::ConfirmationExpired,
        HandoffReason::ExecutionInterrupted,
        HandoffReason::DeadlineElapsed,
    ] {
        let record = record(reason);
        assert!(record.resumable());
        assert!(record.requires_fresh_read, "{reason:?} demands fresh read");
        assert!(
            record.requires_new_confirmation,
            "{reason:?} demands new confirmation"
        );
    }
}

#[test]
fn unrecoverable_handoffs_cannot_resume() {
    for reason in [
        HandoffReason::HandleConsumed,
        HandoffReason::GenerationSuperseded,
        HandoffReason::TargetRemoved,
        HandoffReason::ProfileClosed,
    ] {
        let record = record(reason);
        assert!(!record.resumable(), "{reason:?} must not resume");
        assert!(!record.requires_fresh_read);
        assert!(!record.requires_new_confirmation);
    }
}

#[test]
fn kind_derives_from_reason_and_wire_roundtrips() {
    // The builder derives kind from the reason; there is no override path.
    let record = record(HandoffReason::HandleConsumed);
    assert_eq!(record.kind, HandoffKind::Unrecoverable);
    let wire = serde_json::to_string(&record).expect("serialize");
    let parsed: HandoffRecord = serde_json::from_str(&wire).expect("parse");
    assert_eq!(parsed, record);
    // Wire rejects unknown fields.
    let mut mutated = wire.clone();
    let anchor = "\"action\":\"click\"";
    mutated.insert_str(
        mutated.find(anchor).expect("anchor") + anchor.len(),
        ",\"retry\":true",
    );
    assert!(serde_json::from_str::<HandoffRecord>(&mutated).is_err());
}
