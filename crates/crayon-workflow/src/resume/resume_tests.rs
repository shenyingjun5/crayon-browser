//! WF-005 coverage for the resume gate: matched page resumes, drift / live
//! challenge / unknown side effects terminate, grants must be fresh, and
//! verdicts are idempotent. No assertion input carries page content.

use crate::handoff::{HandoffController, HandoffOutcome};
use crate::resume::{
    EffectKnowledge, GrantFreshness, PageMatch, ResumeDecision, ResumeError, ResumeFacts,
    ResumeGate, TerminationReason,
};
use crayon_domain::{ChallengeEvidence, ChallengeKind, ChallengePhase, ChallengeSession};

const EXPECTED: &str = "https://example.com";

fn awaiting_session(origin: &str) -> ChallengeSession {
    let evidence =
        ChallengeEvidence::new(ChallengeKind::Captcha, origin.to_owned(), None).expect("evidence");
    let mut session = ChallengeSession::detect(evidence).expect("session");
    session.await_human().expect("await");
    session
}

fn matching_facts() -> ResumeFacts {
    ResumeFacts {
        expected_origin: EXPECTED.to_owned(),
        current_origin: EXPECTED.to_owned(),
        challenge_still_present: false,
        page_match: PageMatch::Match,
        effect_knowledge: EffectKnowledge::Known,
        grant: GrantFreshness::Fresh,
    }
}

#[test]
fn matched_world_approves_and_resumes_session() {
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let decision = gate
        .evaluate(&mut session, &matching_facts())
        .expect("gate");
    assert_eq!(decision, ResumeDecision::Approved);
    assert_eq!(session.phase, ChallengePhase::Resumed);
    assert!(gate.decided());
    assert_eq!(gate.decision(), Some(ResumeDecision::Approved));
}

#[test]
fn live_challenge_terminates_before_anything_else() {
    let mut facts = matching_facts();
    facts.challenge_still_present = true;
    facts.current_origin = "https://evil.example".to_owned();
    facts.page_match = PageMatch::Drifted;
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let decision = gate.evaluate(&mut session, &facts).expect("gate");
    assert_eq!(
        decision,
        ResumeDecision::Terminated(TerminationReason::ChallengeStillPresent)
    );
    assert_eq!(session.phase, ChallengePhase::Cancelled);
}

#[test]
fn origin_change_terminates() {
    let mut facts = matching_facts();
    facts.current_origin = "https://other.example".to_owned();
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let decision = gate.evaluate(&mut session, &facts).expect("gate");
    assert_eq!(
        decision,
        ResumeDecision::Terminated(TerminationReason::OriginChanged)
    );
    assert_eq!(session.phase, ChallengePhase::Cancelled);
}

#[test]
fn page_drift_terminates() {
    let mut facts = matching_facts();
    facts.page_match = PageMatch::Drifted;
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let decision = gate.evaluate(&mut session, &facts).expect("gate");
    assert_eq!(
        decision,
        ResumeDecision::Terminated(TerminationReason::PageDrift)
    );
}

#[test]
fn unknown_snapshot_fails_closed_as_drift() {
    let mut facts = matching_facts();
    facts.page_match = PageMatch::Unknown;
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let decision = gate.evaluate(&mut session, &facts).expect("gate");
    assert_eq!(
        decision,
        ResumeDecision::Terminated(TerminationReason::PageDrift)
    );
}

#[test]
fn every_stale_grant_variant_terminates() {
    for grant in [
        GrantFreshness::Expired,
        GrantFreshness::Revoked,
        GrantFreshness::TargetChanged,
        GrantFreshness::Missing,
    ] {
        let mut facts = matching_facts();
        facts.grant = grant;
        let mut session = awaiting_session(EXPECTED);
        let mut gate = ResumeGate::new();
        let decision = gate.evaluate(&mut session, &facts).expect("gate");
        assert_eq!(
            decision,
            ResumeDecision::Terminated(TerminationReason::GrantInvalid),
            "grant variant {grant:?} must terminate"
        );
    }
}

#[test]
fn unknown_side_effect_terminates() {
    let mut facts = matching_facts();
    facts.effect_knowledge = EffectKnowledge::Unknown;
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let decision = gate.evaluate(&mut session, &facts).expect("gate");
    assert_eq!(
        decision,
        ResumeDecision::Terminated(TerminationReason::UnknownSideEffect)
    );
}

#[test]
fn verdict_is_idempotent_and_never_touches_terminal_session() {
    let mut facts = matching_facts();
    facts.page_match = PageMatch::Drifted;
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let first = gate.evaluate(&mut session, &facts).expect("gate");
    let phase_after_first = session.phase;
    for _ in 0..3 {
        let replay = gate.evaluate(&mut session, &facts).expect("gate");
        assert_eq!(replay, first);
        assert_eq!(session.phase, phase_after_first);
    }
}

#[test]
fn approved_gate_replays_without_second_transition() {
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    let first = gate
        .evaluate(&mut session, &matching_facts())
        .expect("gate");
    // Session is now Resumed (terminal); replays must not error or re-write.
    for _ in 0..2 {
        assert_eq!(gate.evaluate(&mut session, &matching_facts()), Ok(first));
    }
    assert_eq!(session.phase, ChallengePhase::Resumed);
}

#[test]
fn non_awaiting_session_is_rejected_without_recording_verdict() {
    // Detected (not yet AwaitingHuman).
    let evidence =
        ChallengeEvidence::new(ChallengeKind::RiskCheck, EXPECTED.to_owned(), None).expect("ev");
    let mut detected = ChallengeSession::detect(evidence).expect("session");
    let mut gate = ResumeGate::new();
    assert_eq!(
        gate.evaluate(&mut detected, &matching_facts()),
        Err(ResumeError::SessionNotAwaiting)
    );
    assert!(!gate.decided());
    assert_eq!(detected.phase, ChallengePhase::Detected);

    // Cancelled terminal session likewise.
    let mut cancelled = awaiting_session(EXPECTED);
    cancelled.cancel().expect("cancel");
    let mut gate2 = ResumeGate::new();
    assert_eq!(
        gate2.evaluate(&mut cancelled, &matching_facts()),
        Err(ResumeError::SessionNotAwaiting)
    );
    assert!(!gate2.decided());
}

#[test]
fn invalid_origins_fail_closed() {
    for (expected, current) in [
        ("not-a-origin", EXPECTED),
        (EXPECTED, "javascript:alert(1)"),
        ("", ""),
    ] {
        let mut facts = matching_facts();
        facts.expected_origin = expected.to_owned();
        facts.current_origin = current.to_owned();
        let mut session = awaiting_session(EXPECTED);
        let mut gate = ResumeGate::new();
        assert_eq!(
            gate.evaluate(&mut session, &facts),
            Err(ResumeError::OriginInvalid),
            "origins {expected:?}/{current:?} must be rejected"
        );
        assert!(!gate.decided());
        assert_eq!(session.phase, ChallengePhase::AwaitingHuman);
    }
}

#[test]
fn handoff_resume_requested_is_the_only_gate_trigger() {
    use crate::resume::requires_resume_gate;
    assert!(requires_resume_gate(HandoffOutcome::ResumeRequested));
    for outcome in [
        HandoffOutcome::AwaitingHuman,
        HandoffOutcome::Cancelled,
        HandoffOutcome::NavigationInvalidated,
        HandoffOutcome::TabClosed,
        HandoffOutcome::Expired,
    ] {
        assert!(!requires_resume_gate(outcome));
    }
}

#[test]
fn gate_integrates_after_real_handoff_controller() {
    // Drive a real WFL-03 controller to ResumeRequested, then run the gate.
    let evidence = ChallengeEvidence::new(ChallengeKind::LoginRequired, EXPECTED.to_owned(), None)
        .expect("ev");
    let mut controller = HandoffController::open(
        evidence,
        crayon_domain::TabId::new("tab-7").expect("tab"),
        crayon_domain::SessionGeneration::from_raw(3),
        1_000,
        30_000,
    )
    .expect("controller");
    assert!(matches!(
        controller.continue_after_human(2_000),
        HandoffOutcome::ResumeRequested
    ));
    // The controller owns its session; emulate the owner handing the live
    // session to the gate after `ResumeRequested`.
    let mut session = awaiting_session(EXPECTED);
    let mut gate = ResumeGate::new();
    assert_eq!(
        gate.evaluate(&mut session, &matching_facts())
            .expect("gate"),
        ResumeDecision::Approved
    );
    assert_eq!(session.phase, ChallengePhase::Resumed);
}
