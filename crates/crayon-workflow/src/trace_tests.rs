use crate::trace::{TraceRecorder, TraceRecorderError, MAX_TRACE_TTL_MS};
use crayon_agent_gateway::{
    grant::{GrantKind, GrantManager, GrantRequest, ProfileScope},
    receipt::{ActionReceipt, ReceiptOutcome},
};
use crayon_domain::{
    ActionKind, AgentCapability, AgentTarget, EffectOutcome, EffectReason, EffectReport, RiskLevel,
    SemanticNodeId, SessionGeneration, TabId, MAX_TRACE_STEPS,
};
use crayon_semantic_action::{ApprovedAction, ConfirmationRef};

const CREATED: u64 = 1_000;
const EXPIRES: u64 = 61_000;

fn tab() -> TabId {
    TabId::new("tab1").expect("tab")
}

fn node() -> SemanticNodeId {
    SemanticNodeId::new("node1").expect("node")
}

fn receipt(outcome: ReceiptOutcome, timestamp_ms: u64) -> ActionReceipt {
    let profile = ProfileScope::new("profile1").expect("profile");
    let target = AgentTarget::Tab { tab: tab() };
    let mut grants = GrantManager::new();
    grants
        .issue(
            GrantRequest {
                kind: GrantKind::Task,
                session: "session1".to_owned(),
                profile: profile.clone(),
                capability: AgentCapability::SemanticAction,
                target: Some(target.clone()),
                task: Some("task1".to_owned()),
                ttl_ms: 60_000,
            },
            CREATED,
        )
        .expect("issue");
    let authorization = grants
        .authorize(
            "session1",
            &profile,
            AgentCapability::SemanticAction,
            Some(&target),
            timestamp_ms,
        )
        .expect("authorize");
    ActionReceipt::new(
        "session1",
        "act.invoke",
        AgentCapability::SemanticAction,
        RiskLevel::R4,
        "tab-tab1",
        authorization.grant,
        outcome,
        None,
        timestamp_ms,
    )
    .expect("receipt")
}

fn approved(action: ActionKind) -> ApprovedAction {
    ApprovedAction {
        node: node(),
        action,
        tab_id: tab(),
        generation: SessionGeneration::from_raw(3),
        deadline_ms: 60_000,
        confirmation: ConfirmationRef::new("confirm1").expect("confirmation"),
    }
}

fn effect(action: ActionKind, outcome: EffectOutcome, revision: u64) -> EffectReport {
    EffectReport::new(
        tab(),
        SessionGeneration::from_raw(3),
        revision,
        action,
        node(),
        outcome,
        (outcome != EffectOutcome::Verified).then_some(EffectReason::Unknown),
        None,
    )
    .expect("effect")
}

fn make_recorder(base_revision: u64) -> TraceRecorder {
    TraceRecorder::new(
        "https://example.com",
        tab(),
        SessionGeneration::from_raw(3),
        base_revision,
        CREATED,
        EXPIRES,
    )
    .expect("recorder")
}

#[test]
fn records_only_three_way_authorized_verified_match() {
    let mut recorder = make_recorder(10);
    recorder
        .record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &effect(ActionKind::Click, EffectOutcome::Verified, 11),
            2_001,
        )
        .expect("record");
    let trace = recorder.finish(2_002).expect("finish");
    assert_eq!(trace.steps.len(), 1);
    assert_eq!(trace.steps[0].summary, "click");
    assert_eq!(trace.steps[0].outcome, EffectOutcome::Verified);
}

#[test]
fn denied_failed_and_indeterminate_never_record() {
    let mut recorder = make_recorder(10);
    for outcome in [
        ReceiptOutcome::Denied,
        ReceiptOutcome::Failed,
        ReceiptOutcome::Cancelled,
    ] {
        assert_eq!(
            recorder.record_verified(
                &receipt(outcome, 2_000),
                &approved(ActionKind::Click),
                &effect(ActionKind::Click, EffectOutcome::Verified, 11),
                2_001,
            ),
            Err(TraceRecorderError::Unauthorized)
        );
    }
    for outcome in [EffectOutcome::Failed, EffectOutcome::Indeterminate] {
        assert_eq!(
            recorder.record_verified(
                &receipt(ReceiptOutcome::Succeeded, 2_000),
                &approved(ActionKind::Click),
                &effect(ActionKind::Click, outcome, 11),
                2_001,
            ),
            Err(TraceRecorderError::NotVerified)
        );
    }
    assert!(recorder.is_empty());
}

#[test]
fn stale_generation_revision_and_mismatched_action_are_rejected() {
    let mut recorder = make_recorder(10);
    let mut stale = effect(ActionKind::Click, EffectOutcome::Verified, 11);
    stale.generation = SessionGeneration::from_raw(2);
    assert_eq!(
        recorder.record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &stale,
            2_001,
        ),
        Err(TraceRecorderError::BindingMismatch)
    );
    let mut wrong_schema = effect(ActionKind::Click, EffectOutcome::Verified, 11);
    wrong_schema.schema_version += 1;
    assert_eq!(
        recorder.record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &wrong_schema,
            2_001,
        ),
        Err(TraceRecorderError::BindingMismatch)
    );
    assert_eq!(
        recorder.record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &effect(ActionKind::Click, EffectOutcome::Verified, 10),
            2_001,
        ),
        Err(TraceRecorderError::StaleResult)
    );
    assert_eq!(
        recorder.record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &effect(ActionKind::Check, EffectOutcome::Verified, 11),
            2_001,
        ),
        Err(TraceRecorderError::BindingMismatch)
    );
}

#[test]
fn ttl_and_discard_drop_the_attempt() {
    assert_eq!(
        TraceRecorder::new(
            "https://example.com",
            tab(),
            SessionGeneration::from_raw(3),
            0,
            CREATED,
            CREATED + MAX_TRACE_TTL_MS + 1,
        )
        .err(),
        Some(TraceRecorderError::InvalidTtl)
    );
    let mut recorder = make_recorder(10);
    assert_eq!(
        recorder.record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &effect(ActionKind::Click, EffectOutcome::Verified, 11),
            EXPIRES,
        ),
        Err(TraceRecorderError::Expired)
    );
    let mut recorder = make_recorder(10);
    recorder
        .record_verified(
            &receipt(ReceiptOutcome::Succeeded, 2_000),
            &approved(ActionKind::Click),
            &effect(ActionKind::Click, EffectOutcome::Verified, 11),
            2_001,
        )
        .expect("record");
    assert_eq!(recorder.discard(), 1);
}

#[test]
fn capacity_is_bounded_and_summary_has_no_caller_text() {
    let mut recorder = make_recorder(0);
    for revision in 1..=MAX_TRACE_STEPS as u64 {
        recorder
            .record_verified(
                &receipt(ReceiptOutcome::Succeeded, 2_000 + revision),
                &approved(ActionKind::SetText),
                &effect(ActionKind::SetText, EffectOutcome::Verified, revision),
                3_000 + revision,
            )
            .expect("within capacity");
    }
    assert_eq!(
        recorder.record_verified(
            &receipt(ReceiptOutcome::Succeeded, 4_000),
            &approved(ActionKind::SetText),
            &effect(
                ActionKind::SetText,
                EffectOutcome::Verified,
                MAX_TRACE_STEPS as u64 + 1,
            ),
            4_001,
        ),
        Err(TraceRecorderError::CapacityExceeded)
    );
    let wire = serde_json::to_string(&recorder.finish(5_000).expect("trace")).expect("wire");
    assert!(!wire.contains("secret-canary"));
    assert!(!wire.contains("selector"));
    assert!(!wire.contains("value"));
}
