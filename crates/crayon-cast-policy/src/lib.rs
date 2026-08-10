//! Pure fail-closed gates for entering cast planning.

use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CapabilityReadiness {
    Unavailable,
    Ready,
}

/// Temporary pre-v1 planning context.
///
/// Capability details remain opaque until their schema is frozen. An
/// unavailable value rejects planning instead of guessing platform support.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlanningContext {
    capability_readiness: CapabilityReadiness,
}

impl PlanningContext {
    #[must_use]
    pub const fn new(capability_readiness: CapabilityReadiness) -> Self {
        Self {
            capability_readiness,
        }
    }
}

/// Result of the pre-v1 playback admission gate.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlanningAdmission {
    Admitted,
    Rejected(PlanningRejection),
}

/// Stable reasons for failing closed before cast planning begins.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PlanningRejection {
    UntrustedObservation,
    MissingUserActivation,
    PlaybackNotAdvanced,
    CapabilitiesUnavailable,
}

/// Admits planning only after every required fact is independently true.
#[must_use]
pub const fn assess_planning(
    observation: PlaybackObservation,
    context: PlanningContext,
) -> PlanningAdmission {
    if !matches!(observation.origin(), ObservationOrigin::BrowserVerified) {
        return PlanningAdmission::Rejected(PlanningRejection::UntrustedObservation);
    }
    if !matches!(
        observation.user_activation(),
        UserActivation::BrowserVerified
    ) {
        return PlanningAdmission::Rejected(PlanningRejection::MissingUserActivation);
    }
    if !matches!(observation.playback_progress(), PlaybackProgress::Advanced) {
        return PlanningAdmission::Rejected(PlanningRejection::PlaybackNotAdvanced);
    }
    if !matches!(context.capability_readiness, CapabilityReadiness::Ready) {
        return PlanningAdmission::Rejected(PlanningRejection::CapabilitiesUnavailable);
    }
    PlanningAdmission::Admitted
}
