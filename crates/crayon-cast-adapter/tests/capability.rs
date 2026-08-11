//! SDK-08 capability cache behaviour and the CS-004 policy golden-input
//! consistency contract, driven deterministically by `FakeCastFacade` and
//! `ManualClock` (no network, no wall clock, no sleeps).

use crayon_cast_adapter::{
    AssessmentStatus, CapabilityCacheConfig, CastCode, CastError, CastFacade, CastMediaKind,
    CastMediaRequest, CastSessionListener, CastSessionRef, CastSessionSnapshot,
    CastSessionSubscription, DeviceState, DiscoveredDevice, PlaybackPosition, ReceiverAssessment,
    ReceiverCapabilityCache, Volume, DEFAULT_ASSESSMENT_TTL, MAX_CACHED_DEVICES,
};
use crayon_cast_policy::{decide, HandoffAvailability, PolicyContext};
use crayon_domain::{DeviceId, ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, CastPolicyInput, ExternalClientHandoff, HandoffReason,
    HeadersClass, MediaCandidate, PageContext, PlaybackState, ProtocolKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;
use std::sync::{Arc, Mutex};
use test_support::cast_facade::{FakeCall, FakeCastFacade};
use test_support::clock::ManualClock;

fn device_id(hex: &str) -> DeviceId {
    DeviceId::new(hex).expect("test device id is valid")
}

fn ready_device(id: &DeviceId, name: &str) -> DiscoveredDevice {
    DiscoveredDevice::new(id.clone(), name.to_string(), DeviceState::Ready, false)
}

fn fake_setup(name: &str) -> (Arc<FakeCastFacade>, DeviceId) {
    let fake = Arc::new(FakeCastFacade::new());
    let id = device_id("0123456789abcdef");
    fake.upsert_device(ready_device(&id, name));
    (fake, id)
}

fn fake_cache(fake: &Arc<FakeCastFacade>, clock: &ManualClock) -> ReceiverCapabilityCache {
    let clock = clock.clone();
    ReceiverCapabilityCache::with_clock(
        fake.clone(),
        CapabilityCacheConfig::default(),
        Arc::new(move || clock.now()),
    )
}

fn assess_call_count(fake: &Arc<FakeCastFacade>) -> usize {
    fake.calls()
        .iter()
        .filter(|call| matches!(call, FakeCall::AssessReceiver(_, _)))
        .count()
}

#[test]
fn fresh_entry_is_served_without_reassessment() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);

    let first = cache.capabilities(&id).expect("assessment succeeds");
    assert!(first.mp4());
    assert!(!first.hls(), "unscripted HLS stays Unknown -> fail closed");
    assert_eq!(assess_call_count(&fake), 2, "one read assesses both kinds");

    let second = cache.capabilities(&id).expect("cached read succeeds");
    assert_eq!(second, first);
    assert_eq!(
        assess_call_count(&fake),
        2,
        "fresh cache hit re-assesses nothing"
    );
}

#[test]
fn expired_entry_reassesses_and_reflects_capability_change() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);

    assert!(cache.capabilities(&id).expect("read").mp4());
    // The receiver profile changes (e.g. receiver app update).
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Unsupported);
    clock.advance(DEFAULT_ASSESSMENT_TTL);

    let caps = cache.capabilities(&id).expect("re-assessment succeeds");
    assert!(
        !caps.mp4(),
        "expired entry must not mask the change (CS-004)"
    );
    assert_eq!(
        assess_call_count(&fake),
        4,
        "expiry forces a fresh assessment"
    );
}

#[test]
fn capability_change_within_ttl_is_picked_up_after_invalidation() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);

    assert!(cache.capabilities(&id).expect("read").mp4());
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Unsupported);

    // Within the TTL the cached fact is still served; the runtime retires it
    // through the epoch bump on the lifecycle event (SDK-12 wiring).
    assert!(cache.capabilities(&id).expect("cached read").mp4());
    cache.invalidate(&id);
    let caps = cache.capabilities(&id).expect("re-assessment succeeds");
    assert!(!caps.mp4(), "invalidation retires the old epoch (CS-004)");
    assert_eq!(assess_call_count(&fake), 4);
}

#[test]
fn risky_and_unknown_are_never_served_as_supported() {
    let (fake, id) = fake_setup("Lebo-like TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Risky);
    fake.set_assessment(&id, CastMediaKind::Hls, AssessmentStatus::Unknown);

    let caps = cache.capabilities(&id).expect("read");
    assert!(!caps.mp4(), "risky is not support (PL-013)");
    assert!(!caps.hls(), "unknown is not support (PL-013)");
}

#[test]
fn failed_refresh_fails_closed_and_caches_nothing() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);
    assert!(cache.capabilities(&id).expect("read").mp4());

    clock.advance(DEFAULT_ASSESSMENT_TTL);
    fake.fail_next_assess_receiver(CastError::NetworkUnavailable);
    assert_eq!(
        cache.capabilities(&id),
        Err(CastError::NetworkUnavailable),
        "refresh failure propagates instead of serving the stale entry"
    );

    // The next read retries the assessment and can succeed; nothing stale
    // was cached in between.
    let caps = cache.capabilities(&id).expect("retry succeeds");
    assert!(caps.mp4());
    assert_eq!(
        assess_call_count(&fake),
        5,
        "2 initial + 1 failed + 2 retry"
    );
}

#[test]
fn aged_out_device_fails_closed_and_replacement_is_reassessed() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);
    assert!(cache.capabilities(&id).expect("read").mp4());

    // The receiver ages out of the discovery snapshot within the TTL.
    fake.remove_device(&id);
    assert_eq!(
        cache.capabilities(&id),
        Err(CastError::DeviceNotFound),
        "an absent device never gets a cached answer"
    );

    // It reappears under the same stable id — possibly a different physical
    // receiver (CS-002) — so the old assessment must not survive.
    fake.upsert_device(ready_device(&id, "Living Room TV"));
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Unsupported);
    let caps = cache.capabilities(&id).expect("re-assessment succeeds");
    assert!(!caps.mp4(), "device replacement forces re-assessment");
}

#[test]
fn invalidate_all_drops_every_device() {
    let (fake, id) = fake_setup("Living Room TV");
    let other = device_id("fedcba9876543210");
    fake.upsert_device(ready_device(&other, "Bedroom TV"));
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);

    cache.capabilities(&id).expect("read");
    cache.capabilities(&other).expect("read");
    assert_eq!(assess_call_count(&fake), 4);

    cache.invalidate_all();
    cache.capabilities(&id).expect("read");
    cache.capabilities(&other).expect("read");
    assert_eq!(assess_call_count(&fake), 8, "every device re-assessed");
}

/// A facade wrapper whose `assess_receiver` runs a one-shot hook before
/// delegating, so a test can invalidate the cache mid-refresh.
struct HookedFacade {
    inner: Arc<FakeCastFacade>,
    hook: Mutex<Option<Box<dyn Fn() + Send>>>,
}

impl HookedFacade {
    fn set_hook(&self, hook: Box<dyn Fn() + Send>) {
        *self.hook.lock().unwrap() = Some(hook);
    }
}

impl CastFacade for HookedFacade {
    fn assess_receiver(
        &self,
        device: &DeviceId,
        media: CastMediaKind,
    ) -> Result<ReceiverAssessment, CastError> {
        if let Some(hook) = self.hook.lock().unwrap().take() {
            hook();
        }
        self.inner.assess_receiver(device, media)
    }

    fn start_discovery(&self) -> Result<(), CastError> {
        self.inner.start_discovery()
    }
    fn stop_discovery(&self) -> Result<(), CastError> {
        self.inner.stop_discovery()
    }
    fn refresh_discovery(&self) -> Result<(), CastError> {
        self.inner.refresh_discovery()
    }
    fn list_devices(&self) -> Vec<DiscoveredDevice> {
        self.inner.list_devices()
    }
    fn is_discovery_running(&self) -> bool {
        self.inner.is_discovery_running()
    }
    fn resolve_device_by_cast_code(&self, code: &CastCode) -> Result<DiscoveredDevice, CastError> {
        self.inner.resolve_device_by_cast_code(code)
    }
    fn connect(&self, device: &DeviceId) -> Result<(), CastError> {
        self.inner.connect(device)
    }
    fn disconnect(&self) {
        self.inner.disconnect();
    }
    fn connected_device(&self) -> Option<DeviceId> {
        self.inner.connected_device()
    }
    fn cast_media(&self, request: &CastMediaRequest) -> Result<CastSessionRef, CastError> {
        self.inner.cast_media(request)
    }
    fn play(&self, session: &CastSessionRef) -> Result<(), CastError> {
        self.inner.play(session)
    }
    fn pause(&self, session: &CastSessionRef) -> Result<(), CastError> {
        self.inner.pause(session)
    }
    fn seek(&self, session: &CastSessionRef, position_seconds: u64) -> Result<(), CastError> {
        self.inner.seek(session, position_seconds)
    }
    fn set_volume(&self, session: &CastSessionRef, volume: Volume) -> Result<(), CastError> {
        self.inner.set_volume(session, volume)
    }
    fn set_muted(&self, session: &CastSessionRef, muted: bool) -> Result<(), CastError> {
        self.inner.set_muted(session, muted)
    }
    fn stop(&self, session: &CastSessionRef) -> Result<(), CastError> {
        self.inner.stop(session)
    }
    fn playback_position(&self, session: &CastSessionRef) -> Result<PlaybackPosition, CastError> {
        self.inner.playback_position(session)
    }
    fn current_session(&self) -> Option<CastSessionSnapshot> {
        self.inner.current_session()
    }
    fn subscribe_session_events(
        &self,
        listener: Arc<dyn CastSessionListener>,
        notify_immediately: bool,
    ) -> Box<dyn CastSessionSubscription> {
        self.inner
            .subscribe_session_events(listener, notify_immediately)
    }
}

#[test]
fn invalidation_during_refresh_discards_the_stale_store() {
    let (fake, id) = fake_setup("Living Room TV");
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);
    let hooked = Arc::new(HookedFacade {
        inner: fake.clone(),
        hook: Mutex::new(None),
    });
    let clock = ManualClock::new();
    let manual = clock.clone();
    let cache = Arc::new(ReceiverCapabilityCache::with_clock(
        hooked.clone(),
        CapabilityCacheConfig::default(),
        Arc::new(move || manual.now()),
    ));

    assert!(cache.capabilities(&id).expect("read").mp4());
    assert_eq!(assess_call_count(&fake), 2);

    // A lifecycle event lands while the next refresh is in flight.
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Unsupported);
    clock.advance(DEFAULT_ASSESSMENT_TTL);
    let weak = Arc::downgrade(&cache);
    let hooked_id = id.clone();
    hooked.set_hook(Box::new(move || {
        if let Some(cache) = weak.upgrade() {
            cache.invalidate(&hooked_id);
        }
    }));

    let caps = cache.capabilities(&id).expect("refresh succeeds");
    assert!(!caps.mp4());
    // The refresh started before the invalidation, so its store must be
    // discarded by the epoch compare-and-set: the next read re-assesses
    // instead of serving the pre-invalidation store.
    cache.capabilities(&id).expect("read");
    assert_eq!(
        assess_call_count(&fake),
        6,
        "stale-generation store must not be cached"
    );
}

#[test]
fn cache_is_bounded_and_full_cache_behaviour_is_explicit() {
    let fake = Arc::new(FakeCastFacade::new());
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);

    // Fill the cache to capacity with fresh entries.
    for index in 0..MAX_CACHED_DEVICES {
        let id = device_id(&format!("{index:016x}"));
        fake.upsert_device(ready_device(&id, &format!("TV {index}")));
        cache.capabilities(&id).expect("read");
    }

    // One more device: one cached entry is evicted (losing a hit is safe)
    // and the new device itself is cached — its second read re-assesses
    // nothing.
    let extra = device_id("ffffffffffffffff");
    fake.upsert_device(ready_device(&extra, "Extra TV"));
    cache.capabilities(&extra).expect("read");
    let calls = assess_call_count(&fake);
    cache.capabilities(&extra).expect("cached read");
    assert_eq!(assess_call_count(&fake), calls, "extra device is cached");

    // A cache holding only invalidation tombstones cannot store: every read
    // still returns the fresh, correct synthesis and re-assesses every time.
    let tombstoned = fake_cache(&fake, &clock);
    for index in 0..MAX_CACHED_DEVICES {
        tombstoned.invalidate(&device_id(&format!("{index:016x}")));
    }
    let before = assess_call_count(&fake);
    let caps = tombstoned.capabilities(&extra).expect("read");
    assert!(!caps.mp4(), "unscripted assessment stays fail closed");
    tombstoned.capabilities(&extra).expect("read");
    assert_eq!(
        assess_call_count(&fake),
        before + 4,
        "a store skipped on a tombstone-only cache re-assesses every read"
    );
}

#[test]
fn concurrent_reads_and_invalidations_never_serve_stale_or_panic() {
    let (fake, id) = fake_setup("Living Room TV");
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);
    let clock = ManualClock::new();
    let cache = Arc::new(fake_cache(&fake, &clock));
    let expected = Ok(crayon_cast_adapter::synthesize_receiver_capabilities(
        AssessmentStatus::Supported,
        AssessmentStatus::Unknown,
    ));

    std::thread::scope(|scope| {
        for _ in 0..4 {
            let cache = Arc::clone(&cache);
            let id = id.clone();
            scope.spawn(move || {
                for _ in 0..50 {
                    assert_eq!(cache.capabilities(&id), expected);
                }
            });
        }
        let cache = Arc::clone(&cache);
        scope.spawn(move || {
            for _ in 0..50 {
                cache.invalidate(&id);
            }
        });
    });
}

// -- CS-004: policy consumes the latest assessment, never a stale cache -----

fn mp4_policy_input(receiver: ReceiverCapabilities) -> CastPolicyInput {
    CastPolicyInput::new(
        PageContext::new(
            TabId::new("tab-cs004").expect("valid tab id"),
            "https://example.com/watch".to_string(),
        ),
        PlaybackState::new(120.0, Some(3600.0), false),
        MediaCandidate::new(
            "https://cdn.example.com/video.mp4".to_string(),
            ProtocolKind::Mp4,
            false,
            HeadersClass::None,
            None,
            None,
            AdContinuity::Preserved,
        ),
        receiver,
    )
}

fn clear_context(handoff: HandoffAvailability) -> PolicyContext {
    PolicyContext {
        observation: PlaybackObservation::new(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
        ),
        protection: Protection::Clear,
        external_client_handoff: handoff,
    }
}

/// CS-004: after a receiver capability change, policy consumes the latest
/// SDK assessment; the old cached input is retired by the epoch bump.
#[test]
fn cs_004_policy_uses_latest_assessment_after_capability_change() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);

    let caps = cache.capabilities(&id).expect("read");
    assert_eq!(
        decide(
            &mp4_policy_input(caps),
            &clear_context(HandoffAvailability::Available)
        ),
        CastPolicyDecision::Direct,
        "Supported receiver takes the Direct route"
    );

    // The receiver's capability changes (e.g. receiver app update); the
    // runtime retires the cached epoch on the lifecycle event.
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Unsupported);
    cache.invalidate(&id);

    let caps = cache.capabilities(&id).expect("re-assessment succeeds");
    assert!(!caps.mp4(), "the stale Supported answer is gone");
    assert_eq!(
        decide(
            &mp4_policy_input(caps),
            &clear_context(HandoffAvailability::Available)
        ),
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::ReceiverIncompatible
        )),
        "policy sees the latest assessment, not the old cache"
    );
    assert_eq!(
        decide(
            &mp4_policy_input(caps),
            &clear_context(HandoffAvailability::Unavailable)
        ),
        CastPolicyDecision::Reject {
            reason: crayon_domain::CoreError::CapabilitiesUnavailable
        },
        "without a handoff surface the same input is a stable rejection"
    );
}

/// CS-004: the TTL backstop alone — with no explicit invalidation, an
/// expired entry is re-assessed and policy still converges on the latest
/// assessment.
#[test]
fn cs_004_expired_cache_never_reaches_policy() {
    let (fake, id) = fake_setup("Living Room TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);
    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Supported);

    let caps = cache.capabilities(&id).expect("read");
    assert_eq!(
        decide(
            &mp4_policy_input(caps),
            &clear_context(HandoffAvailability::Available)
        ),
        CastPolicyDecision::Direct
    );

    fake.set_assessment(&id, CastMediaKind::Video, AssessmentStatus::Unsupported);
    clock.advance(DEFAULT_ASSESSMENT_TTL);

    let caps = cache.capabilities(&id).expect("re-assessment succeeds");
    assert_eq!(
        decide(
            &mp4_policy_input(caps),
            &clear_context(HandoffAvailability::Available)
        ),
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::ReceiverIncompatible
        )),
        "TTL expiry retires the stale golden input"
    );
}

/// CS-004 guard: an `Unknown` receiver fails closed all the way through
/// policy (PL-013) — never a guessed Direct route.
#[test]
fn cs_004_unknown_receiver_fails_closed_through_policy() {
    let (fake, id) = fake_setup("Unprofiled TV");
    let clock = ManualClock::new();
    let cache = fake_cache(&fake, &clock);

    let caps = cache.capabilities(&id).expect("read");
    assert_eq!(
        decide(
            &mp4_policy_input(caps),
            &clear_context(HandoffAvailability::Available)
        ),
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::ReceiverIncompatible
        ))
    );
}
