use crate::handoff::{HandoffController, HandoffError, HandoffOutcome, HandoffReason};
use crayon_domain::{ChallengeEvidence, ChallengeKind, SessionGeneration, TabId};

const NOW: u64 = 10_000;
const EXPIRES: u64 = 70_000;

fn controller(kind: ChallengeKind) -> HandoffController {
    HandoffController::open(
        ChallengeEvidence::new(kind, "https://example.com".to_owned(), None).expect("evidence"),
        TabId::new("tab-7").expect("tab"),
        SessionGeneration::from_raw(3),
        NOW,
        EXPIRES,
    )
    .expect("handoff")
}

#[test]
fn view_is_data_free_accessible_and_never_allows_automation() {
    let mut handoff = controller(ChallengeKind::Captcha);
    let view = handoff.view(NOW);
    assert_eq!(view.reason, HandoffReason::Captcha);
    assert_eq!(view.reason.locale_key(), "workflow.handoff.reason.captcha");
    assert_eq!(view.origin, "https://example.com");
    assert_eq!(view.remaining_ms, 60_000);
    assert_eq!(view.outcome, HandoffOutcome::AwaitingHuman);
    assert!(!view.automation_allowed);
    let debug = format!("{view:?}").to_ascii_lowercase();
    for secret in ["secret-canary", "captcha-answer", "selector", "cookie"] {
        assert!(!debug.contains(secret));
    }
}

#[test]
fn continue_only_requests_revalidation_and_is_terminal() {
    let mut handoff = controller(ChallengeKind::LoginRequired);
    assert_eq!(
        handoff.continue_after_human(NOW + 1),
        HandoffOutcome::ResumeRequested
    );
    assert_eq!(handoff.cancel(NOW + 2), HandoffOutcome::ResumeRequested);
    assert_eq!(
        handoff.on_navigation(NOW + 3),
        HandoffOutcome::ResumeRequested
    );
    assert!(!handoff.view(NOW + 4).automation_allowed);
}

#[test]
fn cancel_navigation_and_close_converge_without_reopening() {
    let cases: [fn(&mut HandoffController, u64) -> HandoffOutcome; 3] = [
        HandoffController::cancel,
        HandoffController::on_navigation,
        HandoffController::on_tab_closed,
    ];
    let expected = [
        HandoffOutcome::Cancelled,
        HandoffOutcome::NavigationInvalidated,
        HandoffOutcome::TabClosed,
    ];
    for (event, expected) in cases.into_iter().zip(expected) {
        let mut handoff = controller(ChallengeKind::RiskCheck);
        assert_eq!(event(&mut handoff, NOW + 1), expected);
        assert_eq!(handoff.continue_after_human(NOW + 2), expected);
        assert_eq!(event(&mut handoff, NOW + 3), expected);
        assert!(!handoff.view(NOW + 4).automation_allowed);
    }
}

#[test]
fn exact_deadline_expires_before_every_user_event() {
    let mut handoff = controller(ChallengeKind::Unknown);
    assert_eq!(
        handoff.continue_after_human(EXPIRES),
        HandoffOutcome::Expired
    );
    assert_eq!(handoff.cancel(EXPIRES + 1), HandoffOutcome::Expired);
    let view = handoff.view(EXPIRES + 2);
    assert_eq!(view.remaining_ms, 0);
    assert_eq!(view.outcome, HandoffOutcome::Expired);
}

#[test]
fn ttl_bounds_fail_closed() {
    let evidence = || {
        ChallengeEvidence::new(
            ChallengeKind::Captcha,
            "https://example.com".to_owned(),
            None,
        )
        .expect("evidence")
    };
    let open = |expires| {
        HandoffController::open(
            evidence(),
            TabId::new("tab-7").expect("tab"),
            SessionGeneration::INITIAL,
            NOW,
            expires,
        )
    };
    assert!(matches!(open(NOW), Err(HandoffError::TtlOutOfBounds)));
    assert!(matches!(
        open(NOW + 300_001),
        Err(HandoffError::TtlOutOfBounds)
    ));
}

#[test]
fn all_reason_variants_have_stable_locale_keys() {
    let cases = [
        (ChallengeKind::Captcha, "workflow.handoff.reason.captcha"),
        (
            ChallengeKind::LoginRequired,
            "workflow.handoff.reason.login_required",
        ),
        (
            ChallengeKind::RiskCheck,
            "workflow.handoff.reason.risk_check",
        ),
        (ChallengeKind::Unknown, "workflow.handoff.reason.unknown"),
    ];
    for (kind, key) in cases {
        assert_eq!(controller(kind).view(NOW).reason.locale_key(), key);
    }
}
