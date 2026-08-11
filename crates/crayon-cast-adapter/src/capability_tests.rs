//! In-crate tests for SDK-08: the synthesis matrix and Fake/real alignment
//! of the cache over `SenderCastFacade` (SDK `add_mock_device` loopback
//! registry, no LAN). Fake-driven behaviour tests live in
//! `tests/capability.rs` (they need `test-support`, which would duplicate
//! this crate in the unit-test dependency graph).

use super::*;
use crate::service::{SenderCastFacade, SenderCastFacadeConfig};
use cast_sender_core::{CastDevice, DeviceDiscoveryState};
use std::sync::atomic::{AtomicU64, Ordering};

/// Deterministic logical clock shared with the cache under test.
#[derive(Clone, Default)]
struct TestClock {
    millis: Arc<AtomicU64>,
}

impl TestClock {
    fn now_fn(&self) -> Arc<dyn Fn() -> Duration + Send + Sync> {
        let clock = self.clone();
        Arc::new(move || Duration::from_millis(clock.millis.load(Ordering::Relaxed)))
    }
}

#[test]
fn synthesis_maps_only_supported_to_true() {
    let statuses = [
        AssessmentStatus::Supported,
        AssessmentStatus::Risky,
        AssessmentStatus::Unsupported,
        AssessmentStatus::Unknown,
    ];
    for video in statuses {
        for hls in statuses {
            let caps = synthesize_receiver_capabilities(video, hls);
            assert_eq!(caps.mp4(), video == AssessmentStatus::Supported);
            assert_eq!(caps.hls(), hls == AssessmentStatus::Supported);
            // The pinned SDK reports no codec/resolution matrix: these are
            // never guessed (fail closed, SDK-08 decision).
            assert!(!caps.dash());
            assert!(!caps.h264());
            assert!(!caps.hevc());
            assert!(!caps.av1());
            assert_eq!(caps.max_height(), 0);
        }
    }
}

// -- Real-facade alignment (SDK `add_mock_device` loopback registry, no LAN) --

/// Mock receiver with caller-controlled CastExtension marker. Control URLs
/// point at dead loopback ports; assessments are pure registry reads.
fn mock_receiver(id: &str, udn: &str, friendly_name: &str, crayon_receiver: bool) -> CastDevice {
    CastDevice {
        id: id.to_string(),
        udn: udn.to_string(),
        friendly_name: friendly_name.to_string(),
        location: "http://127.0.0.1:9/description.xml".to_string(),
        host: "127.0.0.1".to_string(),
        port: Some(9),
        av_transport_control_url: Some("http://127.0.0.1:9/avt".to_string()),
        av_transport_event_sub_url: None,
        rendering_control_url: Some("http://127.0.0.1:9/rc".to_string()),
        cast_extension_control_url: crayon_receiver.then(|| "http://127.0.0.1:9/ce".to_string()),
        capabilities: vec!["urn:schemas-upnp-org:device:MediaRenderer:1".to_string()],
        last_seen_ms: 1,
        last_resolved_ms: Some(1),
        discovery_state: DeviceDiscoveryState::Ready,
        description_error: None,
        is_labi_receiver: false,
        same_host_group_key: String::new(),
        receiver_app: None,
    }
}

fn real_cache(crayon_receiver: bool) -> (ReceiverCapabilityCache, DeviceId) {
    let facade = Arc::new(SenderCastFacade::new(SenderCastFacadeConfig::default()));
    let device = mock_receiver("sdk08-mock", "uuid:sdk08-mock", "SDK08 TV", crayon_receiver);
    let id = DeviceId::new(&device.stable_device_key()).expect("stable key is a valid id");
    facade
        .service()
        .expect("facade is live")
        .add_mock_device(device);
    let clock = TestClock::default();
    let cache = ReceiverCapabilityCache::with_clock(
        facade,
        CapabilityCacheConfig::default(),
        clock.now_fn(),
    );
    (cache, id)
}

#[test]
fn real_facade_unknown_profile_fails_closed_like_the_fake() {
    let (cache, id) = real_cache(false);
    let caps = cache.capabilities(&id).expect("assessment succeeds");
    assert_eq!(
        caps,
        synthesize_receiver_capabilities(AssessmentStatus::Unknown, AssessmentStatus::Unknown),
        "no built-in profile -> Unknown on both kinds -> all false"
    );
    // The cache-hit path is identical over the real facade.
    assert_eq!(cache.capabilities(&id).expect("cached read"), caps);
}

#[test]
fn real_facade_crayon_receiver_synthesizes_supported() {
    let (cache, id) = real_cache(true);
    let caps = cache.capabilities(&id).expect("assessment succeeds");
    assert!(caps.mp4(), "Labi receiver profile assesses Video Supported");
    assert!(caps.hls(), "Labi receiver profile assesses HLS Supported");
    assert!(
        !caps.h264() && !caps.hevc() && !caps.av1() && caps.max_height() == 0,
        "the pinned SDK has no codec/resolution matrix; never guessed"
    );
}
