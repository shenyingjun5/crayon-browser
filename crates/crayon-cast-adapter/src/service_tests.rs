//! SDK-05 lifecycle and session-bridge tests for `SenderCastFacade`.
//!
//! Network policy: no test sends LAN traffic. Discovery start and cast-code
//! resolution multicast/fetch on the LAN and are verified manually
//! (recorded in the roadmap); everything here uses construction without
//! `start_discovery`, SDK-registered loopback mock devices (TCP connect to
//! `127.0.0.1:9` fails fast with connection-refused), or the SDK's
//! platform self-session entry point, which only binds an ephemeral
//! loopback session-control server.
//!
//! Asynchronous events arrive on the SDK hub dispatch thread; tests wait on
//! condvars with deadlines instead of fixed sleeps.

use super::*;
use crate::dto::CastMediaUrl;
use cast_sender_core::{CastDevice, DeviceDiscoveryState};
use cast_sender_session::MediaKind as SdkMediaKind;
use std::sync::{Condvar, Mutex as StdMutex};
use std::time::{Duration, Instant};

/// Deadline for event-waiting assertions; generous for loaded CI hosts,
/// only ever hit on failure.
const EVENT_TIMEOUT: Duration = Duration::from_secs(10);

fn facade() -> SenderCastFacade {
    SenderCastFacade::new(SenderCastFacadeConfig::default())
}

fn session_ref(session_id: &str, generation: u64) -> CastSessionRef {
    CastSessionRef::new(
        SessionId::new(session_id).expect("test session id is valid"),
        SessionGeneration::from_raw(generation),
    )
}

/// Ready, connectable receiver whose control URLs point at a dead loopback
/// port: TCP connect fails immediately with connection-refused, so tests
/// exercise the SDK control path without LAN traffic or hangs.
fn loopback_device(id: &str, friendly_name: &str) -> CastDevice {
    CastDevice {
        id: id.to_string(),
        udn: format!("uuid:{id}"),
        friendly_name: friendly_name.to_string(),
        location: "http://127.0.0.1:9/description.xml".to_string(),
        host: "127.0.0.1".to_string(),
        port: Some(9),
        av_transport_control_url: Some("http://127.0.0.1:9/avt".to_string()),
        av_transport_event_sub_url: None,
        rendering_control_url: Some("http://127.0.0.1:9/rc".to_string()),
        cast_extension_control_url: None,
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

/// Registers a loopback mock device through the SDK's public test entry
/// point and returns its product `DeviceId` (the SDK stable device key).
fn register_loopback_device(facade: &SenderCastFacade, id: &str) -> DeviceId {
    let service = facade.service().expect("facade is live");
    let device = loopback_device(id, "SDK05 Living Room");
    let device_id = DeviceId::new(&device.stable_device_key()).expect("stable key is a valid id");
    service.add_mock_device(device);
    device_id
}

/// Recording listener with condvar wakeup; also records whether the
/// callback observed a re-entrant facade call succeed.
#[derive(Default)]
struct Recording {
    events: StdMutex<Vec<CastSessionSnapshot>>,
    wake: Condvar,
}

impl Recording {
    fn push(&self, snapshot: CastSessionSnapshot) {
        self.events
            .lock()
            .expect("recording poisoned")
            .push(snapshot);
        self.wake.notify_all();
    }

    fn snapshots(&self) -> Vec<CastSessionSnapshot> {
        self.events.lock().expect("recording poisoned").clone()
    }

    /// Waits until `predicate` matches the recorded events or the deadline
    /// passes; returns the matching snapshots.
    fn wait_until(
        &self,
        predicate: impl Fn(&[CastSessionSnapshot]) -> bool,
    ) -> Vec<CastSessionSnapshot> {
        let deadline = Instant::now() + EVENT_TIMEOUT;
        let mut events = self.events.lock().expect("recording poisoned");
        loop {
            if predicate(&events) {
                return events.clone();
            }
            let remaining = deadline.saturating_duration_since(Instant::now());
            assert!(!remaining.is_zero(), "timed out waiting for session event");
            let (guard, timeout) = self
                .wake
                .wait_timeout(events, remaining)
                .expect("recording poisoned");
            events = guard;
            assert!(
                !timeout.timed_out() || predicate(&events),
                "event wait timed out"
            );
        }
    }
}

impl CastSessionListener for Recording {
    fn on_session_changed(&self, snapshot: CastSessionSnapshot) {
        self.push(snapshot);
    }
}

// -- Construction and empty state -------------------------------------------

#[test]
fn construction_has_no_runtime_side_effects() {
    let facade = facade();
    assert!(!facade.is_discovery_running());
    assert!(facade.list_devices().is_empty());
    assert_eq!(facade.connected_device(), None);
    assert_eq!(facade.current_session(), None);
}

#[test]
fn facade_is_send_sync_and_object_safe() {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<SenderCastFacade>();
    let facade: Arc<dyn CastFacade> = Arc::new(facade());
    assert!(!facade.is_discovery_running());
}

// -- Idempotent lifecycle without discovery start -----------------------------
// `start_discovery` spawns the SDK SSDP worker (LAN multicast) and is
// verified manually; everything reachable without it lives here.

#[test]
fn repeated_stop_discovery_without_start_is_idempotent() {
    let facade = facade();
    facade.stop_discovery().expect("first stop");
    facade.stop_discovery().expect("second stop");
    assert!(!facade.is_discovery_running());
}

#[test]
fn disconnect_without_connection_is_idempotent_noop() {
    let facade = facade();
    facade.disconnect();
    facade.disconnect();
    assert_eq!(facade.connected_device(), None);
}

#[test]
fn shutdown_is_idempotent_and_every_call_fails_closed() {
    let facade = facade();
    facade.shutdown();
    facade.shutdown();

    let session = session_ref("cast-deadbeef", 1);
    assert_eq!(facade.play(&session), Err(CastError::InvalidState));
    assert_eq!(facade.pause(&session), Err(CastError::InvalidState));
    assert_eq!(facade.seek(&session, 0), Err(CastError::InvalidState));
    assert_eq!(
        facade.set_volume(&session, Volume::new(50).expect("valid volume")),
        Err(CastError::InvalidState)
    );
    assert_eq!(
        facade.set_muted(&session, true),
        Err(CastError::InvalidState)
    );
    assert_eq!(facade.stop(&session), Err(CastError::InvalidState));
    assert_eq!(
        facade.playback_position(&session),
        Err(CastError::InvalidState)
    );
    assert_eq!(facade.start_discovery(), Err(CastError::InvalidState));
    assert_eq!(facade.stop_discovery(), Err(CastError::InvalidState));
    assert_eq!(facade.refresh_discovery(), Err(CastError::InvalidState));
    assert_eq!(
        facade.connect(&DeviceId::new("deadbeefdeadbeef").expect("valid id")),
        Err(CastError::InvalidState)
    );
    assert_eq!(
        facade.assess_receiver(
            &DeviceId::new("deadbeefdeadbeef").expect("valid id"),
            CastMediaKind::Video,
        ),
        Err(CastError::InvalidState)
    );
    let code = CastCode::new("ABC123").expect("valid code");
    assert_eq!(
        facade.resolve_device_by_cast_code(&code),
        Err(CastError::InvalidState)
    );
    let request = CastMediaRequest::new(
        DeviceId::new("deadbeefdeadbeef").expect("valid id"),
        DeliveryProtocol::Mp4,
        CastMediaUrl::new("http://127.0.0.1:9/video.mp4").expect("valid url"),
    );
    assert_eq!(facade.cast_media(&request), Err(CastError::InvalidState));

    // Infallible reads degrade to empty instead of panicking.
    assert!(facade.list_devices().is_empty());
    assert!(!facade.is_discovery_running());
    assert_eq!(facade.connected_device(), None);
    assert_eq!(facade.current_session(), None);
    facade.disconnect();

    // Subscribing after shutdown yields an inert handle; dropping it is the
    // idempotent unsubscribe.
    let recording = Arc::new(Recording::default());
    let subscription = facade.subscribe_session_events(recording.clone(), true);
    drop(subscription);
    assert!(recording.snapshots().is_empty());
}

#[test]
fn drop_after_live_session_releases_cleanly_and_restart_works() {
    {
        let facade = facade();
        let service = facade.service().expect("facade is live");
        service
            .begin_platform_self_receiver_session(
                "sdk05drop",
                SdkMediaKind::Video,
                "http://127.0.0.1:9/control",
            )
            .expect("self session starts");
        assert!(facade.current_session().is_some());
        // Drop with a live supervised session and a running loopback control
        // server: must not panic, hang, or leak the port.
    }
    // Restart: a fresh instance binds ephemeral ports, so no conflict with
    // the dropped instance is possible.
    let restarted = facade();
    let service = restarted.service().expect("facade is live");
    service
        .begin_platform_self_receiver_session(
            "sdk05restart",
            SdkMediaKind::Video,
            "http://127.0.0.1:9/control",
        )
        .expect("restart self session starts");
    let current = restarted.current_session().expect("restart has a session");
    assert_eq!(current.session().session_id().as_str(), "sdk05restart");
    assert!(
        restarted.list_devices().is_empty(),
        "state must not carry over"
    );
}

// -- Fail-closed validation and fencing without a session ---------------------

#[test]
fn controls_fail_closed_without_any_session() {
    let facade = facade();
    let session = session_ref("cast-deadbeef", 1);
    assert_eq!(facade.play(&session), Err(CastError::NoActiveSession));
    assert_eq!(facade.pause(&session), Err(CastError::NoActiveSession));
    assert_eq!(facade.seek(&session, 30), Err(CastError::NoActiveSession));
    assert_eq!(
        facade.set_volume(&session, Volume::new(10).expect("valid volume")),
        Err(CastError::NoActiveSession)
    );
    assert_eq!(
        facade.set_muted(&session, false),
        Err(CastError::NoActiveSession)
    );
    assert_eq!(facade.stop(&session), Err(CastError::NoActiveSession));
    assert_eq!(
        facade.playback_position(&session),
        Err(CastError::NoActiveSession)
    );
}

#[test]
fn cast_media_without_connection_fails_closed() {
    let facade = facade();
    let request = CastMediaRequest::new(
        DeviceId::new("deadbeefdeadbeef").expect("valid id"),
        DeliveryProtocol::Mp4,
        CastMediaUrl::new("http://127.0.0.1:9/video.mp4").expect("valid url"),
    );
    assert_eq!(facade.cast_media(&request), Err(CastError::InvalidState));
}

#[test]
fn unknown_device_operations_fail_closed() {
    let facade = facade();
    let unknown = DeviceId::new("deadbeefdeadbeef").expect("valid id");
    assert_eq!(facade.connect(&unknown), Err(CastError::DeviceNotFound));
    assert_eq!(
        facade.assess_receiver(&unknown, CastMediaKind::Video),
        Err(CastError::DeviceNotFound)
    );
    assert_eq!(
        facade.assess_receiver(&unknown, CastMediaKind::Hls),
        Err(CastError::DeviceNotFound)
    );
}

// -- Connection over SDK-registered loopback devices ---------------------------

#[test]
fn connect_disconnect_roundtrip_with_registered_device() {
    let facade = facade();
    let device_id = register_loopback_device(&facade, "sdk05-mock-connect");

    let listed = facade.list_devices();
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].device_id(), &device_id);
    assert_eq!(listed[0].friendly_name(), "SDK05 Living Room");
    assert_eq!(listed[0].state(), DeviceState::Ready);
    assert!(!listed[0].is_crayon_receiver());

    facade.connect(&device_id).expect("connect succeeds");
    assert_eq!(facade.connected_device(), Some(device_id.clone()));

    facade.disconnect();
    assert_eq!(facade.connected_device(), None);
    facade.disconnect();
}

#[test]
fn cast_media_to_other_device_is_rejected_before_reaching_sdk() {
    let facade = facade();
    let device_id = register_loopback_device(&facade, "sdk05-mock-guard");
    facade.connect(&device_id).expect("connect succeeds");

    let request = CastMediaRequest::new(
        DeviceId::new("deadbeefdeadbeef").expect("valid id"),
        DeliveryProtocol::Mp4,
        CastMediaUrl::new("http://127.0.0.1:9/video.mp4").expect("valid url"),
    );
    assert_eq!(facade.cast_media(&request), Err(CastError::InvalidState));
    assert_eq!(facade.current_session(), None, "no session may be created");
}

#[test]
fn cast_media_to_unreachable_loopback_receiver_maps_the_sdk_error() {
    let facade = facade();
    let device_id = register_loopback_device(&facade, "sdk05-mock-cast");
    facade.connect(&device_id).expect("connect succeeds");

    let request = CastMediaRequest::new(
        device_id,
        DeliveryProtocol::Mp4,
        CastMediaUrl::new("http://127.0.0.1:9/video.mp4").expect("valid url"),
    );
    // The receiver endpoint refuses the loopback TCP connect; the SDK
    // reports Network/SOAP_CONNECT_FAILED, which CS-008 maps to
    // NetworkUnavailable — never a panic or a raw SDK string.
    assert_eq!(
        facade.cast_media(&request),
        Err(CastError::NetworkUnavailable)
    );
    assert_eq!(
        facade.current_session(),
        None,
        "failed cast creates no session"
    );
}

#[test]
fn assess_receiver_delegates_to_sdk_assessment() {
    let facade = facade();
    let device_id = register_loopback_device(&facade, "sdk05-mock-assess");
    // No built-in renderer profile matches the mock device: the SDK answers
    // Unknown, which the product presents as fail-closed (PL-013).
    let assessment = facade
        .assess_receiver(&device_id, CastMediaKind::Video)
        .expect("assessment succeeds");
    assert_eq!(assessment.device_id(), &device_id);
    assert_eq!(assessment.media(), CastMediaKind::Video);
    assert_eq!(assessment.status(), AssessmentStatus::Unknown);
}

// -- Session supervision bridge ------------------------------------------------

/// Drives one supervised session lifecycle through the SDK's public
/// platform entry points (loopback control server only) and asserts the
/// facade's snapshot conversion, event bridging, generation fencing and
/// stop idempotency.
#[test]
fn session_bridge_flow_fencing_and_stop_idempotency() {
    let facade = facade();
    let service = facade.service().expect("facade is live");
    let recording = Arc::new(Recording::default());
    let subscription = facade.subscribe_session_events(recording.clone(), false);

    let first = service
        .begin_platform_self_receiver_session(
            "sdk05bridge",
            SdkMediaKind::Video,
            "http://127.0.0.1:9/control",
        )
        .expect("first session starts");
    let first_ref = session_ref("sdk05bridge", first.handle.generation);

    let current = facade.current_session().expect("session is current");
    assert_eq!(current.session(), &first_ref);
    assert_eq!(current.phase(), CastSessionPhase::Starting);
    assert_eq!(current.playback(), CastPlaybackState::Preparing);
    assert_eq!(current.terminal_reason(), None);
    assert!(!current.is_terminal());

    // The bridge delivers the Starting event on the hub dispatch thread.
    let events = recording.wait_until(|events| !events.is_empty());
    assert_eq!(events[0].session(), &first_ref);
    assert_eq!(events[0].phase(), CastSessionPhase::Starting);

    service
        .mark_platform_session_playing(&first.handle)
        .expect("mark playing");
    let events = recording.wait_until(|events| {
        events
            .iter()
            .any(|event| event.playback() == CastPlaybackState::Playing)
    });
    let playing = events
        .iter()
        .find(|event| event.playback() == CastPlaybackState::Playing)
        .expect("playing event recorded");
    assert_eq!(playing.session(), &first_ref);
    assert!(
        playing.state_revision() > events[0].state_revision(),
        "revision advances within the generation"
    );

    // Fencing before the SDK: an unknown newer-generation handle and a
    // foreign same-generation handle are both rejected as `NoActiveSession`
    // and never reach the receiver (mirroring the SDK-04 fake).
    let unknown_newer = session_ref("sdk05bridge", first.handle.generation + 100);
    assert_eq!(facade.stop(&unknown_newer), Err(CastError::NoActiveSession));
    let foreign = session_ref("sdk05foreign", first.handle.generation);
    assert_eq!(facade.play(&foreign), Err(CastError::NoActiveSession));

    // stop is idempotent: first stop terminates, second reports success.
    facade.stop(&first_ref).expect("stop succeeds");
    let events = recording.wait_until(|events| {
        events.iter().any(|event| {
            event.is_terminal()
                && event.terminal_reason() == Some(CastTerminalReason::StoppedBySender)
        })
    });
    let terminal = events
        .iter()
        .find(|event| event.is_terminal())
        .expect("terminal event recorded");
    assert_eq!(terminal.session(), &first_ref);
    assert_eq!(terminal.playback(), CastPlaybackState::Stopped);
    facade
        .stop(&first_ref)
        .expect("stop on terminal is idempotent");

    // A terminated session rejects every other control.
    assert_eq!(facade.play(&first_ref), Err(CastError::NoActiveSession));
    assert_eq!(
        facade.playback_position(&first_ref),
        Err(CastError::NoActiveSession)
    );

    // Replacement: a new session supersedes; the old generation is fenced.
    let second = service
        .begin_platform_self_receiver_session(
            "sdk05bridge2",
            SdkMediaKind::Hls,
            "http://127.0.0.1:9/control",
        )
        .expect("second session starts");
    assert!(second.handle.generation > first.handle.generation);
    recording.wait_until(|events| {
        events
            .iter()
            .any(|event| event.session().generation().get() == second.handle.generation)
    });
    let current = facade.current_session().expect("second session current");
    assert_eq!(
        current.session().generation().get(),
        second.handle.generation
    );

    assert_eq!(
        facade.stop(&first_ref),
        Err(CastError::StaleSessionGeneration)
    );
    assert_eq!(
        facade.play(&first_ref),
        Err(CastError::StaleSessionGeneration)
    );

    // Every delivered event is generation-monotonic: an old-generation
    // event never arrives after a newer one (CS-007, enforced by the SDK
    // hub before the bridge).
    let generations: Vec<u64> = recording
        .snapshots()
        .iter()
        .map(|event| event.session().generation().get())
        .collect();
    assert!(
        generations.windows(2).all(|pair| pair[0] <= pair[1]),
        "generations must be non-decreasing: {generations:?}"
    );

    drop(subscription);
}

#[test]
fn unsubscribe_stops_delivery_and_notify_immediately_targets_current() {
    let facade = facade();
    let service = facade.service().expect("facade is live");
    let dropped = Arc::new(Recording::default());
    let subscription = facade.subscribe_session_events(dropped.clone(), false);

    service
        .begin_platform_self_receiver_session(
            "sdk05unsub",
            SdkMediaKind::Video,
            "http://127.0.0.1:9/control",
        )
        .expect("first session starts");
    dropped.wait_until(|events| events.len() == 1);
    drop(subscription);

    // A fresh notify-immediately subscription receives the current snapshot
    // once, then keeps receiving newer generations.
    let live = Arc::new(Recording::default());
    let _live_subscription = facade.subscribe_session_events(live.clone(), true);
    live.wait_until(|events| !events.is_empty());

    let second = service
        .begin_platform_self_receiver_session(
            "sdk05unsub2",
            SdkMediaKind::Video,
            "http://127.0.0.1:9/control",
        )
        .expect("second session starts");
    // The hub broadcasts the second session to all live listeners in one
    // batch; once the live listener has it, the dropped listener provably
    // missed it (it would have been in the same batch).
    live.wait_until(|events| {
        events
            .iter()
            .any(|event| event.session().generation().get() == second.handle.generation)
    });
    assert!(
        dropped
            .snapshots()
            .iter()
            .all(|event| event.session().generation().get() < second.handle.generation),
        "dropped subscription must not receive newer events"
    );
}

#[test]
fn listener_may_reenter_the_facade_from_the_callback() {
    let facade = Arc::new(facade());
    let reentrant = Arc::new(ReentrantListener {
        facade: Arc::downgrade(&facade),
        completed: StdMutex::new(false),
        wake: Condvar::new(),
    });
    let _subscription = facade.subscribe_session_events(reentrant.clone(), false);

    let service = facade.service().expect("facade is live");
    service
        .begin_platform_self_receiver_session(
            "sdk05reenter",
            SdkMediaKind::Video,
            "http://127.0.0.1:9/control",
        )
        .expect("session starts");

    // If the bridge held any adapter/SDK lock during the callback, the
    // re-entrant reads would deadlock and this wait would time out.
    let deadline = Instant::now() + EVENT_TIMEOUT;
    let mut completed = reentrant.completed.lock().expect("completed poisoned");
    while !*completed {
        let remaining = deadline.saturating_duration_since(Instant::now());
        assert!(
            !remaining.is_zero(),
            "callback deadlocked on facade re-entry"
        );
        let (guard, timeout) = reentrant
            .wake
            .wait_timeout(completed, remaining)
            .expect("completed poisoned");
        completed = guard;
        assert!(
            !timeout.timed_out() || *completed,
            "callback re-entry timed out"
        );
    }
}

struct ReentrantListener {
    facade: std::sync::Weak<SenderCastFacade>,
    completed: StdMutex<bool>,
    wake: Condvar,
}

impl CastSessionListener for ReentrantListener {
    fn on_session_changed(&self, _snapshot: CastSessionSnapshot) {
        let facade = self.facade.upgrade().expect("facade alive during test");
        // Re-entrant reads across every facade state path.
        let _ = facade.current_session();
        let _ = facade.list_devices();
        let _ = facade.is_discovery_running();
        let _ = facade.connected_device();
        *self.completed.lock().expect("completed poisoned") = true;
        self.wake.notify_all();
    }
}

// -- Concurrent shutdown --------------------------------------------------------

#[test]
fn concurrent_calls_and_shutdown_never_panic_or_deadlock() {
    let facade = Arc::new(facade());
    let session = session_ref("cast-deadbeef", 1);
    std::thread::scope(|scope| {
        for _ in 0..4 {
            scope.spawn(|| {
                for _ in 0..50 {
                    // Either the pre-shutdown fencing error or the
                    // post-shutdown fail-closed error; never a panic.
                    assert!(matches!(
                        facade.play(&session),
                        Err(CastError::NoActiveSession | CastError::InvalidState)
                    ));
                    let _ = facade.list_devices();
                    let _ = facade.current_session();
                }
            });
        }
        scope.spawn(|| {
            for _ in 0..50 {
                facade.disconnect();
            }
        });
        facade.shutdown();
    });
    assert_eq!(
        facade.play(&session),
        Err(CastError::InvalidState),
        "after shutdown every call fails closed"
    );
}

// -- Conversion mapping pins -----------------------------------------------------

#[test]
fn snapshot_conversion_maps_every_phase_playback_and_reason() {
    let phases: &[(SdkSessionPhase, CastSessionPhase)] = &[
        (SdkSessionPhase::Starting, CastSessionPhase::Starting),
        (SdkSessionPhase::Active, CastSessionPhase::Active),
        (SdkSessionPhase::Suspended, CastSessionPhase::Suspended),
        (SdkSessionPhase::Recovering, CastSessionPhase::Recovering),
        (SdkSessionPhase::Terminating, CastSessionPhase::Terminating),
        (SdkSessionPhase::Terminated, CastSessionPhase::Terminated),
    ];
    let playbacks: &[(SdkPlaybackState, CastPlaybackState)] = &[
        (SdkPlaybackState::Unknown, CastPlaybackState::Unknown),
        (SdkPlaybackState::Preparing, CastPlaybackState::Preparing),
        (SdkPlaybackState::Buffering, CastPlaybackState::Buffering),
        (SdkPlaybackState::Playing, CastPlaybackState::Playing),
        (SdkPlaybackState::Paused, CastPlaybackState::Paused),
        // No product counterpart: the facade never delivers images.
        (
            SdkPlaybackState::PresentingStatic,
            CastPlaybackState::Unknown,
        ),
        (SdkPlaybackState::Ended, CastPlaybackState::Ended),
        (SdkPlaybackState::Stopped, CastPlaybackState::Stopped),
        (SdkPlaybackState::Failed, CastPlaybackState::Failed),
    ];
    let reasons: &[(SdkTerminalReason, CastTerminalReason)] = &[
        (
            SdkTerminalReason::StoppedBySender,
            CastTerminalReason::StoppedBySender,
        ),
        (
            SdkTerminalReason::StoppedByReceiver,
            CastTerminalReason::StoppedByReceiver,
        ),
        (
            SdkTerminalReason::EndedNormally,
            CastTerminalReason::EndedNormally,
        ),
        (
            SdkTerminalReason::ReplacedByNewCast,
            CastTerminalReason::ReplacedByNewCast,
        ),
        (
            SdkTerminalReason::ReplacedByOtherController,
            CastTerminalReason::ReplacedByOtherController,
        ),
        (
            SdkTerminalReason::ReceiverShutdown,
            CastTerminalReason::ReceiverShutdown,
        ),
        (
            SdkTerminalReason::ReceiverSessionLost,
            CastTerminalReason::ReceiverSessionLost,
        ),
        (
            SdkTerminalReason::ReceiverUnreachable,
            CastTerminalReason::ReceiverUnreachable,
        ),
        (
            SdkTerminalReason::PlaybackFailed,
            CastTerminalReason::PlaybackFailed,
        ),
        (
            SdkTerminalReason::SourceFailed,
            CastTerminalReason::SourceFailed,
        ),
        (
            SdkTerminalReason::ProtocolError,
            CastTerminalReason::ProtocolError,
        ),
    ];
    for (sdk, expected) in phases {
        assert_eq!(session_phase_of(*sdk), *expected);
    }
    for (sdk, expected) in playbacks {
        assert_eq!(playback_state_of(*sdk), *expected);
    }
    for (sdk, expected) in reasons {
        assert_eq!(terminal_reason_of(*sdk), *expected);
    }
}

#[test]
fn snapshot_conversion_drops_sdk_internal_fields() {
    let sdk = cast_sender_session::CastSessionSnapshot {
        handle: CastSessionHandle::new("cast-abc123", 7, SdkMediaKind::Video)
            .expect("valid handle"),
        phase: SdkSessionPhase::Terminated,
        playback_state: SdkPlaybackState::Failed,
        health: cast_sender_session::SessionHealth::Unreachable,
        ownership: cast_sender_session::Ownership::OtherController,
        receiver_kind: cast_sender_session::ReceiverKind::ThirdParty,
        receiver_connection: cast_sender_session::ReceiverConnection::Unavailable,
        media_revision: 3,
        state_revision: 9,
        terminal_reason: Some(SdkTerminalReason::ReceiverUnreachable),
        error_code: Some("SOME_SDK_CODE".to_string()),
        started_at_elapsed_ms: 1,
        last_changed_at_elapsed_ms: 2,
        ended_at_elapsed_ms: Some(3),
        receiver_generation: Some(4),
        receiver_epoch_id: Some("epoch".to_string()),
    };
    let mapped = session_snapshot_of(&sdk).expect("snapshot converts");
    assert_eq!(mapped.session().session_id().as_str(), "cast-abc123");
    assert_eq!(mapped.session().generation().get(), 7);
    assert_eq!(mapped.phase(), CastSessionPhase::Terminated);
    assert_eq!(mapped.playback(), CastPlaybackState::Failed);
    assert_eq!(mapped.state_revision(), 9);
    assert_eq!(
        mapped.terminal_reason(),
        Some(CastTerminalReason::ReceiverUnreachable)
    );
}

#[test]
fn device_mapping_uses_stable_key_and_never_exposes_locators() {
    let device = loopback_device("sdk05-mock-mapping", "SDK05 Mapping TV");
    let expected_key = device.stable_device_key();
    let mapped = discovered_device_of(&device).expect("device converts");
    assert_eq!(mapped.device_id().as_str(), expected_key);
    assert_eq!(mapped.friendly_name(), "SDK05 Mapping TV");
    assert_eq!(mapped.state(), DeviceState::Ready);
    assert!(!mapped.is_crayon_receiver());
    // Structural privacy: the DTO has no host/location/port/UDN fields, and
    // its serialized form contains only the four contract fields (CS-002).
    let json = serde_json::to_value(&mapped).expect("device serializes");
    let mut keys: Vec<&str> = json
        .as_object()
        .expect("device is an object")
        .keys()
        .map(String::as_str)
        .collect();
    keys.sort_unstable();
    assert_eq!(
        keys,
        ["device_id", "friendly_name", "is_crayon_receiver", "state"]
    );
}

#[test]
fn device_state_mapping_is_exhaustive() {
    let cases: &[(DeviceDiscoveryState, DeviceState)] = &[
        (DeviceDiscoveryState::Ready, DeviceState::Ready),
        (DeviceDiscoveryState::Incomplete, DeviceState::Incomplete),
        (
            DeviceDiscoveryState::RequiresAuthorization,
            DeviceState::RequiresAuthorization,
        ),
        (DeviceDiscoveryState::Stale, DeviceState::Stale),
        (DeviceDiscoveryState::Offline, DeviceState::Offline),
    ];
    for (sdk, expected) in cases {
        assert_eq!(device_state_of(sdk), *expected);
    }
}

#[test]
fn error_mapping_delegates_to_the_cs008_table() {
    let cases: &[(CastSenderError, CastError)] = &[
        (
            CastSenderError::new(ErrorKind::State, "CAST_SESSION_STALE_GENERATION", "stale"),
            CastError::StaleSessionGeneration,
        ),
        (
            CastSenderError::new(
                ErrorKind::State,
                "CAST_SESSION_ALREADY_TERMINATED",
                "terminated",
            ),
            CastError::NoActiveSession,
        ),
        (
            CastSenderError::new(ErrorKind::Network, "SOAP_CONNECT_FAILED", "refused"),
            CastError::NetworkUnavailable,
        ),
        (
            CastSenderError::new(ErrorKind::Device, "DEVICE_NOT_FOUND", "missing"),
            CastError::DeviceNotFound,
        ),
        (
            CastSenderError::new(ErrorKind::Control, "SOME_FUTURE_CODE", "future"),
            CastError::ReceiverProtocol,
        ),
    ];
    for (sdk_error, expected) in cases {
        assert_eq!(
            map_error(sdk_error.clone()),
            *expected,
            "code {}",
            sdk_error.code
        );
    }
}
