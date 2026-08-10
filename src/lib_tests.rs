use super::*;

#[test]
fn encode_component() {
    assert_eq!(
        encode_url_component("https://a.com/x y.m3u8?token=a&b=1"),
        "https%3A%2F%2Fa.com%2Fx%20y.m3u8%3Ftoken%3Da%26b%3D1"
    );
}

#[test]
fn formal_runtime_is_available_through_compatibility_re_exports() {
    let runtime = RuntimeDescriptor::formal("Crayon").expect("valid product identity");

    assert_eq!(runtime.identity().name(), "Crayon");
    assert_eq!(runtime.identity().mode(), ProductMode::Formal);
    assert_eq!(runtime.handshake().schema_version(), SchemaVersion::CURRENT);
}

#[test]
fn media_probe_is_available_through_formal_re_export() {
    assert_eq!(media_probe::codec_name("hvc1.1.6.L93.B0"), "HEVC");
    assert_eq!(
        media_probe::hls_container("#EXTM3U\n#EXTINF:5,\nsegment.ts\n"),
        Some("TS")
    );
}

#[test]
fn observation_and_policy_are_available_through_formal_re_exports() {
    let observation = media_observer::PlaybackObservation::new(
        media_observer::ObservationOrigin::PageReported,
        media_observer::UserActivation::BrowserVerified,
        media_observer::PlaybackProgress::Advanced,
    );

    assert_eq!(
        cast_policy::assess_planning(
            observation,
            cast_policy::PlanningContext::new(cast_policy::CapabilityReadiness::Ready)
        ),
        cast_policy::PlanningAdmission::Rejected(
            cast_policy::PlanningRejection::UntrustedObservation
        )
    );
}
