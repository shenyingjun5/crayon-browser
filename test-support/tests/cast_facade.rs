//! FakeCastFacade self-tests (SDK-04): every orchestration capability across
//! normal, boundary, repeated, cancel/error and stale-generation cases.

use crayon_cast_adapter::{
    AssessmentStatus, CastCode, CastError, CastFacade, CastMediaKind, CastMediaRequest,
    CastMediaUrl, CastPlaybackState, CastSessionPhase, CastSessionRef, CastSessionSnapshot,
    CastTerminalReason, DeliveryProtocol, DeviceState, DiscoveredDevice, PlaybackPosition, Volume,
};
use crayon_domain::{DeviceId, SessionGeneration, SessionId};
use std::sync::{Arc, Mutex};
use test_support::cast_facade::{FakeCall, FakeCastFacade};

fn device(id: &str) -> DeviceId {
    DeviceId::new(id).expect("test device id")
}

fn discovered(id: &str, name: &str, state: DeviceState) -> DiscoveredDevice {
    DiscoveredDevice::new(device(id), name.to_owned(), state, false)
}

fn media_request(id: &str) -> CastMediaRequest {
    CastMediaRequest::new(
        device(id),
        DeliveryProtocol::Mp4,
        CastMediaUrl::new("https://relay.local/s/example-token/m.mp4").expect("test url"),
    )
}

/// Collects session snapshots in receive order.
#[derive(Clone, Default)]
struct Recorder {
    events: Arc<Mutex<Vec<CastSessionSnapshot>>>,
}

impl Recorder {
    fn listener(&self) -> Arc<dyn crayon_cast_adapter::CastSessionListener> {
        let events = Arc::clone(&self.events);
        Arc::new(move |snapshot| events.lock().unwrap().push(snapshot))
    }

    fn taken(&self) -> Vec<CastSessionSnapshot> {
        self.events.lock().unwrap().clone()
    }
}

fn connected_fake() -> (FakeCastFacade, DeviceId) {
    let fake = FakeCastFacade::new();
    let id = device("dev-01");
    fake.upsert_device(discovered("dev-01", "Living Room", DeviceState::Ready));
    fake.start_discovery().expect("start discovery");
    fake.connect(&id).expect("connect");
    (fake, id)
}

#[test]
fn discovery_lifecycle_is_idempotent_and_gates_nothing() {
    let fake = FakeCastFacade::new();
    assert!(!fake.is_discovery_running());
    fake.start_discovery().expect("first start");
    fake.start_discovery()
        .expect("repeated start is not an error");
    assert!(fake.is_discovery_running());
    fake.refresh_discovery().expect("refresh while running");
    fake.stop_discovery().expect("stop");
    fake.stop_discovery().expect("repeated stop is a no-op");
    assert!(!fake.is_discovery_running());
    assert_eq!(
        fake.calls(),
        vec![
            FakeCall::StartDiscovery,
            FakeCall::StartDiscovery,
            FakeCall::RefreshDiscovery,
            FakeCall::StopDiscovery,
            FakeCall::StopDiscovery,
        ]
    );
}

#[test]
fn discovery_snapshot_upsert_replaces_same_id_and_remove_drops() {
    let fake = FakeCastFacade::new();
    fake.upsert_device(discovered("dev-01", "Old Name", DeviceState::Incomplete));
    // Same stable id re-announced (IP/interface change): one entry, new data.
    fake.upsert_device(discovered("dev-01", "New Name", DeviceState::Ready));
    fake.upsert_device(discovered("dev-02", "Kitchen", DeviceState::Ready));
    let devices = fake.list_devices();
    assert_eq!(devices.len(), 2);
    let updated = devices
        .iter()
        .find(|d| d.device_id() == &device("dev-01"))
        .expect("dev-01 present");
    assert_eq!(updated.friendly_name(), "New Name");
    assert_eq!(updated.state(), DeviceState::Ready);
    fake.remove_device(&device("dev-01"));
    assert_eq!(fake.list_devices().len(), 1);
}

/// SDK-06 alignment: the snapshot exposes connectable receivers only, keeps
/// the facade's deterministic total order, and survives `stop_discovery`.
#[test]
fn discovery_snapshot_hides_non_ready_orders_deterministically_and_survives_stop() {
    let fake = FakeCastFacade::new();
    fake.upsert_device(discovered("dev-zeta", "Zeta", DeviceState::Ready));
    fake.upsert_device(discovered("dev-alpha", "Alpha", DeviceState::Ready));
    fake.upsert_device(discovered("dev-stale", "Stale TV", DeviceState::Stale));
    fake.upsert_device(discovered("dev-off", "Offline TV", DeviceState::Offline));
    fake.upsert_device(discovered("dev-inc", "Half TV", DeviceState::Incomplete));

    let snapshot = fake.list_devices();
    let ids: Vec<&str> = snapshot
        .iter()
        .map(|device| device.device_id().as_str())
        .collect();
    assert_eq!(ids, ["dev-alpha", "dev-zeta"], "ready only, name-ordered");

    fake.stop_discovery().expect("stop");
    fake.stop_discovery().expect("repeated stop is a no-op");
    assert_eq!(
        fake.list_devices(),
        snapshot,
        "stop never clears the snapshot"
    );

    // An aged-out device stays in the registry but is absent from the
    // snapshot, so connect reports it as not found (SDK-07 alignment with
    // the real facade); it re-enters the snapshot once it resolves again.
    assert_eq!(
        fake.connect(&device("dev-stale")),
        Err(CastError::DeviceNotFound)
    );
    fake.upsert_device(discovered("dev-stale", "Stale TV", DeviceState::Ready));
    assert!(fake
        .list_devices()
        .iter()
        .any(|entry| entry.device_id() == &device("dev-stale")));
}

#[test]
fn scripted_start_failure_is_one_shot_and_preserves_state() {
    let fake = FakeCastFacade::new();
    fake.fail_next_start_discovery(CastError::NetworkUnavailable);
    assert_eq!(
        fake.start_discovery(),
        Err(CastError::NetworkUnavailable),
        "scripted error surfaces"
    );
    assert!(
        !fake.is_discovery_running(),
        "failed start keeps discovery off"
    );
    fake.start_discovery().expect("one-shot error consumed");
    assert!(fake.is_discovery_running());
}

#[test]
fn cast_code_resolves_bound_device_and_reports_unbound() {
    let fake = FakeCastFacade::new();
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Ready));
    let code = CastCode::new("ab-12 cd").expect("normalized code");
    assert_eq!(
        fake.resolve_device_by_cast_code(&code),
        Err(CastError::DeviceNotFound),
        "unbound code"
    );
    fake.bind_cast_code(&code, &device("dev-01"));
    let resolved = fake.resolve_device_by_cast_code(&code).expect("bound code");
    assert_eq!(resolved.device_id(), &device("dev-01"));
    assert_eq!(resolved.friendly_name(), "TV");
}

#[test]
fn cast_code_scripted_error_is_consumed_once() {
    let fake = FakeCastFacade::new();
    let code = CastCode::new("ABC123").expect("code");
    fake.bind_cast_code(&code, &device("dev-01"));
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Ready));
    fake.fail_next_resolve_cast_code(CastError::ReceiverUnreachable);
    assert_eq!(
        fake.resolve_device_by_cast_code(&code),
        Err(CastError::ReceiverUnreachable)
    );
    assert!(fake.resolve_device_by_cast_code(&code).is_ok());
}

#[test]
fn cast_code_bound_to_removed_device_reports_not_found() {
    let fake = FakeCastFacade::new();
    let code = CastCode::new("ABC123").expect("code");
    fake.bind_cast_code(&code, &device("dev-01"));
    assert_eq!(
        fake.resolve_device_by_cast_code(&code),
        Err(CastError::DeviceNotFound),
        "binding to an unknown device cannot resolve"
    );
}

#[test]
fn connect_rejects_unknown_and_expired_devices() {
    let fake = FakeCastFacade::new();
    assert_eq!(
        fake.connect(&device("dev-99")),
        Err(CastError::DeviceNotFound)
    );
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Stale));
    assert_eq!(
        fake.connect(&device("dev-01")),
        Err(CastError::DeviceNotFound),
        "an aged-out device is absent from the snapshot (SDK-07 alignment)"
    );
    assert_eq!(fake.connected_device(), None);
}

#[test]
fn connect_route_lost_is_scripted_one_shot() {
    let (fake, id) = connected_fake();
    // The route-expired-but-visible branch of the real facade; the fake has
    // no route-TTL concept, so it is orchestrated (SDK-07).
    fake.fail_next_connect(CastError::RouteLost);
    assert_eq!(fake.connect(&id), Err(CastError::RouteLost));
    assert_eq!(
        fake.connected_device(),
        Some(id.clone()),
        "a failed connect does not drop the current connection"
    );
    fake.connect(&id).expect("one-shot error consumed");
}

#[test]
fn connect_is_idempotent_and_switches_devices() {
    let fake = FakeCastFacade::new();
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Ready));
    fake.upsert_device(discovered("dev-02", "Kitchen", DeviceState::Ready));
    fake.connect(&device("dev-01")).expect("first connect");
    fake.connect(&device("dev-01"))
        .expect("repeated connect to the same device is idempotent");
    assert_eq!(fake.connected_device(), Some(device("dev-01")));
    fake.connect(&device("dev-02"))
        .expect("connecting another device switches");
    assert_eq!(fake.connected_device(), Some(device("dev-02")));
    // Disconnect then reconnect is an ordinary fresh connect.
    fake.disconnect();
    assert_eq!(fake.connected_device(), None);
    fake.connect(&device("dev-01")).expect("reconnect");
    assert_eq!(fake.connected_device(), Some(device("dev-01")));
}

#[test]
fn disconnect_is_idempotent_and_tears_down_active_session() {
    let (fake, id) = connected_fake();
    fake.disconnect();
    fake.disconnect();
    assert_eq!(fake.connected_device(), None);
    fake.connect(&id).expect("reconnect");
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    let recorder = Recorder::default();
    let _subscription = fake.subscribe_session_events(recorder.listener(), false);
    fake.disconnect();
    let terminal = fake.current_session().expect("session snapshot kept");
    assert!(terminal.is_terminal());
    assert_eq!(
        terminal.terminal_reason(),
        Some(CastTerminalReason::StoppedBySender)
    );
    assert_eq!(
        recorder.taken().last().expect("terminal event").session(),
        &session
    );
}

#[test]
fn assessment_defaults_unknown_and_reflects_latest_scripted_value() {
    let fake = FakeCastFacade::new();
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Ready));
    let initial = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Video)
        .expect("assessment");
    assert_eq!(initial.status(), AssessmentStatus::Unknown);
    fake.set_assessment(
        &device("dev-01"),
        CastMediaKind::Video,
        AssessmentStatus::Supported,
    );
    fake.set_assessment(
        &device("dev-01"),
        CastMediaKind::Video,
        AssessmentStatus::Unsupported,
    );
    let latest = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Video)
        .expect("assessment");
    assert_eq!(latest.status(), AssessmentStatus::Unsupported);
    // Media kinds are independent facts.
    let hls = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Hls)
        .expect("assessment");
    assert_eq!(hls.status(), AssessmentStatus::Unknown);
    assert_eq!(
        fake.assess_receiver(&device("dev-99"), CastMediaKind::Video),
        Err(CastError::DeviceNotFound)
    );
}

#[test]
fn cast_media_requires_matching_connected_device() {
    let (fake, _) = connected_fake();
    // Not connected at all.
    let stray = FakeCastFacade::new();
    assert_eq!(
        stray.cast_media(&media_request("dev-01")),
        Err(CastError::InvalidState)
    );
    // Connected to another device than the request targets.
    fake.upsert_device(discovered("dev-02", "Kitchen", DeviceState::Ready));
    assert_eq!(
        fake.cast_media(&media_request("dev-02")),
        Err(CastError::InvalidState)
    );
    assert!(
        !fake
            .calls()
            .iter()
            .any(|call| matches!(call, FakeCall::CastMedia { .. })),
        "fail-closed validation never reaches the receiver"
    );
}

#[test]
fn cast_media_success_emits_starting_then_active_and_records_url() {
    let (fake, _) = connected_fake();
    let recorder = Recorder::default();
    let _subscription = fake.subscribe_session_events(recorder.listener(), false);
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    assert_eq!(
        session.session_id(),
        &SessionId::new("fake-session-1").expect("id")
    );
    assert_eq!(session.generation(), SessionGeneration::INITIAL);
    let events = recorder.taken();
    assert_eq!(events.len(), 2);
    assert_eq!(events[0].phase(), CastSessionPhase::Starting);
    assert_eq!(events[0].state_revision(), 0);
    assert_eq!(events[1].phase(), CastSessionPhase::Active);
    assert_eq!(events[1].playback(), CastPlaybackState::Playing);
    assert_eq!(events[1].state_revision(), 1);
    assert_eq!(
        fake.calls().last(),
        Some(&FakeCall::CastMedia {
            device: device("dev-01"),
            protocol: DeliveryProtocol::Mp4,
            url: "https://relay.local/s/example-token/m.mp4".to_owned(),
        }),
        "URL forwarded unchanged (asserted via getter; type is not serializable)"
    );
}

#[test]
fn cast_media_replacement_terminates_previous_and_advances_generation() {
    let (fake, _) = connected_fake();
    let recorder = Recorder::default();
    let _subscription = fake.subscribe_session_events(recorder.listener(), false);
    let first = fake
        .cast_media(&media_request("dev-01"))
        .expect("first cast");
    let second = fake
        .cast_media(&media_request("dev-01"))
        .expect("second cast");
    assert!(second.generation().supersedes(first.generation()));
    assert_ne!(second.session_id(), first.session_id());
    let events = recorder.taken();
    let replaced = events
        .iter()
        .find(|event| event.terminal_reason() == Some(CastTerminalReason::ReplacedByNewCast))
        .expect("previous session reported replacement");
    assert_eq!(replaced.session(), &first);
    assert!(replaced.is_terminal());
    // Controls on the replaced session are fenced.
    assert_eq!(fake.pause(&first), Err(CastError::StaleSessionGeneration));
}

#[test]
fn scripted_cast_media_failure_creates_no_session() {
    let (fake, _) = connected_fake();
    fake.fail_next_cast_media(CastError::CastStartFailed);
    assert_eq!(
        fake.cast_media(&media_request("dev-01")),
        Err(CastError::CastStartFailed)
    );
    assert_eq!(fake.current_session(), None);
    assert!(fake.cast_media(&media_request("dev-01")).is_ok());
}

#[test]
fn playback_controls_update_state_and_emit_revisions() {
    let (fake, _) = connected_fake();
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    let recorder = Recorder::default();
    let _subscription = fake.subscribe_session_events(recorder.listener(), false);
    fake.pause(&session).expect("pause");
    assert_eq!(
        fake.current_session().expect("snapshot").playback(),
        CastPlaybackState::Paused
    );
    fake.play(&session).expect("play");
    fake.seek(&session, 42).expect("seek");
    assert_eq!(
        fake.playback_position(&session).expect("position"),
        PlaybackPosition::new(Some(42), None)
    );
    fake.set_volume(&session, Volume::new(80).expect("volume"))
        .expect("set volume");
    fake.set_muted(&session, true).expect("mute");
    let revisions: Vec<u64> = recorder
        .taken()
        .iter()
        .map(|e| e.state_revision())
        .collect();
    assert_eq!(
        revisions,
        vec![2, 3, 4, 5, 6],
        "each control bumps revision"
    );
    assert!(fake.calls().contains(&FakeCall::Seek {
        session: session.clone(),
        position_seconds: 42,
    }));
    assert!(fake.calls().contains(&FakeCall::SetVolume {
        session: session.clone(),
        volume: 80,
    }));
    assert!(fake.calls().contains(&FakeCall::SetMuted {
        session: session.clone(),
        muted: true,
    }));
}

#[test]
fn scripted_control_error_is_one_shot() {
    let (fake, _) = connected_fake();
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    fake.fail_next_control(CastError::ReceiverProtocol);
    assert_eq!(fake.pause(&session), Err(CastError::ReceiverProtocol));
    fake.pause(&session).expect("scripted error consumed");
}

#[test]
fn stale_and_foreign_handles_are_fenced_without_reaching_receiver() {
    let (fake, _) = connected_fake();
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    // Foreign session identity at the same generation.
    let foreign = CastSessionRef::new(
        SessionId::new("fake-session-999").expect("id"),
        session.generation(),
    );
    assert_eq!(fake.play(&foreign), Err(CastError::NoActiveSession));
    assert!(
        !fake
            .calls()
            .iter()
            .any(|call| matches!(call, FakeCall::Play(_))),
        "foreign handles never reach the receiver"
    );
    // Older generation after replacement.
    let second = fake
        .cast_media(&media_request("dev-01"))
        .expect("second cast");
    assert_eq!(fake.play(&session), Err(CastError::StaleSessionGeneration));
    assert_eq!(fake.stop(&session), Err(CastError::StaleSessionGeneration));
    assert!(
        !fake.calls().iter().any(|call| matches!(
            call,
            FakeCall::Play(s) | FakeCall::Stop(s) if s == &session
        )),
        "stale handles never reach the receiver"
    );
    fake.play(&second).expect("current generation works");
}

#[test]
fn stop_is_idempotent_on_terminal_but_rejects_foreign() {
    let (fake, _) = connected_fake();
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    fake.stop(&session).expect("first stop");
    fake.stop(&session)
        .expect("already terminal is idempotent success");
    let foreign = CastSessionRef::new(
        SessionId::new("fake-session-999").expect("id"),
        session.generation(),
    );
    assert_eq!(fake.stop(&foreign), Err(CastError::NoActiveSession));
    // Other controls on the terminated session still fail.
    assert_eq!(fake.pause(&session), Err(CastError::NoActiveSession));
    assert_eq!(
        fake.playback_position(&session),
        Err(CastError::NoActiveSession)
    );
}

#[test]
fn controls_without_any_session_report_no_active_session() {
    let fake = FakeCastFacade::new();
    let session = CastSessionRef::new(
        SessionId::new("fake-session-1").expect("id"),
        SessionGeneration::INITIAL,
    );
    assert_eq!(fake.play(&session), Err(CastError::NoActiveSession));
    assert_eq!(fake.stop(&session), Err(CastError::NoActiveSession));
}

#[test]
fn terminal_simulations_converge_with_distinct_reasons() {
    // The expected (playback, reason) pairs mirror the pinned SDK terminal
    // mapping 1:1 (`terminate_snapshot`) so Fake and real facade produce the
    // same product-observable terminal snapshot per scenario (CS-007).
    for (drive, expected_playback, expected_reason) in [
        (
            FakeCastFacade::simulate_natural_end as fn(&FakeCastFacade),
            CastPlaybackState::Ended,
            CastTerminalReason::EndedNormally,
        ),
        (
            FakeCastFacade::simulate_receiver_stop,
            CastPlaybackState::Stopped,
            CastTerminalReason::StoppedByReceiver,
        ),
        (
            FakeCastFacade::simulate_route_lost,
            CastPlaybackState::Stopped,
            CastTerminalReason::ReceiverUnreachable,
        ),
        (
            FakeCastFacade::simulate_replaced_by_other_controller,
            CastPlaybackState::Stopped,
            CastTerminalReason::ReplacedByOtherController,
        ),
    ] {
        let (fake, _) = connected_fake();
        fake.cast_media(&media_request("dev-01")).expect("cast");
        drive(&fake);
        let snapshot = fake.current_session().expect("snapshot");
        assert!(snapshot.is_terminal());
        assert_eq!(snapshot.playback(), expected_playback);
        assert_eq!(snapshot.terminal_reason(), Some(expected_reason));
    }
    // Driving without a session is a no-op, not a panic.
    let idle = FakeCastFacade::new();
    idle.simulate_route_lost();
    assert_eq!(idle.current_session(), None);
}

#[test]
fn drive_session_ignores_already_terminal_session() {
    let (fake, _) = connected_fake();
    fake.cast_media(&media_request("dev-01")).expect("cast");
    fake.simulate_natural_end();
    let terminal_revision = fake.current_session().expect("snapshot").state_revision();
    fake.drive_session(CastSessionPhase::Active, CastPlaybackState::Playing, None);
    let snapshot = fake.current_session().expect("snapshot");
    assert!(
        snapshot.is_terminal(),
        "terminated sessions stay terminated"
    );
    assert_eq!(snapshot.state_revision(), terminal_revision);
}

#[test]
fn subscription_immediate_notify_and_drop_semantics() {
    let (fake, _) = connected_fake();
    fake.cast_media(&media_request("dev-01")).expect("cast");
    let recorder = Recorder::default();
    {
        let subscription = fake.subscribe_session_events(recorder.listener(), true);
        assert_eq!(fake.listener_count(), 1);
        assert_eq!(recorder.taken().len(), 1, "current snapshot delivered once");
        drop(subscription);
        assert_eq!(fake.listener_count(), 0, "drop unsubscribes");
    }
    fake.simulate_natural_end();
    assert_eq!(recorder.taken().len(), 1, "no events after unsubscribe");
    // Without immediate notify nothing is delivered at subscribe time.
    let late = Recorder::default();
    let _subscription = fake.subscribe_session_events(late.listener(), false);
    assert!(late.taken().is_empty());
}

#[test]
fn pushed_stale_snapshot_reaches_listeners_but_never_current_session() {
    let (fake, _) = connected_fake();
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    let recorder = Recorder::default();
    let _subscription = fake.subscribe_session_events(recorder.listener(), false);
    // Stale-generation event from an already-replaced session.
    let second = fake
        .cast_media(&media_request("dev-01"))
        .expect("second cast");
    let stale_event = CastSessionSnapshot::new(
        session.clone(),
        CastSessionPhase::Terminated,
        CastPlaybackState::Failed,
        99,
        Some(CastTerminalReason::ProtocolError),
    );
    fake.push_session_snapshot(stale_event.clone());
    let current = fake.current_session().expect("snapshot");
    assert_eq!(
        current.session(),
        &second,
        "stale event cannot replace current"
    );
    assert!(!current.is_terminal());
    assert!(
        recorder.taken().contains(&stale_event),
        "listeners still see the stale event; consumers own fencing"
    );
}

#[test]
fn pushed_superseding_snapshot_is_adopted_as_current() {
    let (fake, _) = connected_fake();
    let session = fake.cast_media(&media_request("dev-01")).expect("cast");
    let newer = CastSessionSnapshot::new(
        session,
        CastSessionPhase::Terminated,
        CastPlaybackState::Ended,
        7,
        Some(CastTerminalReason::EndedNormally),
    );
    fake.push_session_snapshot(newer.clone());
    assert_eq!(fake.current_session(), Some(newer));
}
