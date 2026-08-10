use crayon_cast_policy::{
    assess_planning, CapabilityReadiness, PlanningAdmission, PlanningContext, PlanningRejection,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};

fn assess(
    origin: ObservationOrigin,
    user_activation: UserActivation,
    playback_progress: PlaybackProgress,
    capability_readiness: CapabilityReadiness,
) -> PlanningAdmission {
    assess_planning(
        PlaybackObservation::new(origin, user_activation, playback_progress),
        PlanningContext::new(capability_readiness),
    )
}

#[test]
fn browser_verified_user_playback_with_capabilities_is_admitted() {
    assert_eq!(
        assess(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
            CapabilityReadiness::Ready,
        ),
        PlanningAdmission::Admitted
    );
}

#[test]
fn page_report_cannot_authorize_cast_planning() {
    assert_eq!(
        assess(
            ObservationOrigin::PageReported,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
            CapabilityReadiness::Ready,
        ),
        PlanningAdmission::Rejected(PlanningRejection::UntrustedObservation)
    );
}

#[test]
fn missing_user_activation_or_playback_progress_fails_closed() {
    assert_eq!(
        assess(
            ObservationOrigin::BrowserVerified,
            UserActivation::Missing,
            PlaybackProgress::Advanced,
            CapabilityReadiness::Ready,
        ),
        PlanningAdmission::Rejected(PlanningRejection::MissingUserActivation)
    );
    assert_eq!(
        assess(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::NotAdvanced,
            CapabilityReadiness::Ready,
        ),
        PlanningAdmission::Rejected(PlanningRejection::PlaybackNotAdvanced)
    );
}

#[test]
fn unavailable_capabilities_fail_closed() {
    assert_eq!(
        assess(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
            CapabilityReadiness::Unavailable,
        ),
        PlanningAdmission::Rejected(PlanningRejection::CapabilitiesUnavailable)
    );
}
