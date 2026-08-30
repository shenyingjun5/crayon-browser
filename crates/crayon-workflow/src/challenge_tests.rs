use crate::challenge::{ChallengeDetector, ChallengeSignals};
use crayon_domain::{ChallengeKind, SemanticSchemaError};

const ORIGIN: &str = "https://example.com";

#[test]
fn classifies_closed_challenge_fixtures_deterministically() {
    let detector = ChallengeDetector;
    let cases = [
        (
            ChallengeSignals {
                captcha_surface: true,
                ..ChallengeSignals::default()
            },
            ChallengeKind::Captcha,
            "browser_signal:captcha",
        ),
        (
            ChallengeSignals {
                interactive_verification: true,
                ..ChallengeSignals::default()
            },
            ChallengeKind::Captcha,
            "browser_signal:captcha",
        ),
        (
            ChallengeSignals {
                risk_interstitial: true,
                ..ChallengeSignals::default()
            },
            ChallengeKind::RiskCheck,
            "browser_signal:risk_check",
        ),
        (
            ChallengeSignals {
                login_required: true,
                ..ChallengeSignals::default()
            },
            ChallengeKind::LoginRequired,
            "browser_signal:login_required",
        ),
    ];

    for (signals, expected_kind, expected_note) in cases {
        let evidence = detector
            .detect(ORIGIN, signals)
            .expect("valid origin")
            .expect("challenge detected");
        assert_eq!(evidence.kind, expected_kind);
        assert_eq!(evidence.origin, ORIGIN);
        assert_eq!(evidence.note.as_deref(), Some(expected_note));
    }
}

#[test]
fn similar_non_challenge_page_does_not_pause() {
    assert_eq!(
        ChallengeDetector.detect(ORIGIN, ChallengeSignals::default()),
        Ok(None)
    );
}

#[test]
fn simultaneous_signals_use_conservative_stable_precedence() {
    let evidence = ChallengeDetector
        .detect(
            ORIGIN,
            ChallengeSignals {
                captcha_surface: true,
                interactive_verification: true,
                risk_interstitial: true,
                login_required: true,
            },
        )
        .expect("valid")
        .expect("detected");
    assert_eq!(evidence.kind, ChallengeKind::Captcha);
}

#[test]
fn invalid_origin_fails_closed_when_a_signal_is_present() {
    assert_eq!(
        ChallengeDetector.detect(
            "javascript:alert(1)",
            ChallengeSignals {
                login_required: true,
                ..ChallengeSignals::default()
            }
        ),
        Err(SemanticSchemaError::OriginInvalid)
    );
    assert_eq!(
        ChallengeDetector.detect("file:///tmp/page", ChallengeSignals::default()),
        Err(SemanticSchemaError::OriginInvalid)
    );
}

#[test]
fn evidence_has_no_solving_or_page_data_surface() {
    let evidence = ChallengeDetector
        .detect(
            ORIGIN,
            ChallengeSignals {
                captcha_surface: true,
                ..ChallengeSignals::default()
            },
        )
        .expect("valid")
        .expect("detected");
    let wire = serde_json::to_string(&evidence).expect("serialize");
    for forbidden in [
        "solution",
        "answer",
        "solver",
        "bypass",
        "selector",
        "html",
        "cookie",
        "authorization",
    ] {
        assert!(!wire.to_ascii_lowercase().contains(forbidden));
    }
}
