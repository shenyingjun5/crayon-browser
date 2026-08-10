use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};

#[test]
fn observation_preserves_provenance_and_playback_facts() {
    let observation = PlaybackObservation::new(
        ObservationOrigin::BrowserVerified,
        UserActivation::BrowserVerified,
        PlaybackProgress::Advanced,
    );

    assert_eq!(observation.origin(), ObservationOrigin::BrowserVerified);
    assert_eq!(
        observation.user_activation(),
        UserActivation::BrowserVerified
    );
    assert_eq!(observation.playback_progress(), PlaybackProgress::Advanced);
}

#[test]
fn page_report_is_represented_as_untrusted_provenance() {
    let observation = PlaybackObservation::new(
        ObservationOrigin::PageReported,
        UserActivation::BrowserVerified,
        PlaybackProgress::Advanced,
    );

    assert_eq!(observation.origin(), ObservationOrigin::PageReported);
}
