use super::*;

#[test]
fn request_purpose_matrix() {
    assert!(HandoffRequest::new(
        HandoffReason::NoRouteAvailable,
        HandoffAction::LaunchClient,
        "cast-handoff"
    )
    .is_ok());
    for bad in ["", "bad purpose!", &"p".repeat(33)] {
        assert_eq!(
            HandoffRequest::new(
                HandoffReason::UserChoice,
                HandoffAction::DownloadClient,
                bad
            )
            .unwrap_err(),
            HandoffError::Unavailable,
            "{bad:?}"
        );
    }
}

#[test]
fn outcome_set_is_closed_and_mirror_free() {
    // Exhaustive match pins the closed set; adding a "mirroring started"
    // variant would fail compilation here.
    for outcome in [
        HandoffOutcome::DownloadStarted,
        HandoffOutcome::LaunchRequested,
        HandoffOutcome::NotInstalled,
        HandoffOutcome::Cancelled,
        HandoffOutcome::Failed,
    ] {
        let described = match outcome {
            HandoffOutcome::DownloadStarted => "download_started",
            HandoffOutcome::LaunchRequested => "launch_requested",
            HandoffOutcome::NotInstalled => "not_installed",
            HandoffOutcome::Cancelled => "cancelled",
            HandoffOutcome::Failed => "failed",
        };
        assert!(!described.contains("mirror"));
    }
}

#[test]
fn error_display_golden() {
    assert_eq!(
        HandoffError::NotConfirmed.to_string(),
        "external client handoff was not confirmed by the user"
    );
    assert_eq!(
        HandoffError::Unavailable.to_string(),
        "external client handoff unavailable"
    );
}
