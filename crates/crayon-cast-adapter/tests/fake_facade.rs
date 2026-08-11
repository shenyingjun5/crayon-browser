//! SDK-04 demonstrative coverage: every CS-001..CS-009 scenario is
//! deterministically orchestrated against the scripted `FakeCastFacade`
//! (test-support). Test names keep the case IDs.

use crayon_cast_adapter::{
    AssessmentStatus, CastCode, CastError, CastFacade, CastMediaKind, CastMediaRequest,
    CastMediaUrl, CastPlaybackState, CastSessionPhase, CastSessionSnapshot, CastTerminalReason,
    DeliveryProtocol, DeviceState, DiscoveredDevice, Volume,
};
use crayon_domain::DeviceId;
use std::sync::{Arc, Mutex};
use test_support::cast_facade::{FakeCall, FakeCastFacade};

fn device(id: &str) -> DeviceId {
    DeviceId::new(id).expect("test device id")
}

fn discovered(id: &str, name: &str, state: DeviceState) -> DiscoveredDevice {
    DiscoveredDevice::new(device(id), name.to_owned(), state, false)
}

fn direct_mp4_request(id: &str) -> CastMediaRequest {
    CastMediaRequest::new(
        device(id),
        DeliveryProtocol::Mp4,
        CastMediaUrl::new("https://cdn.example/v/film.mp4?sig=example").expect("test url"),
    )
}

fn collect_events() -> (
    Arc<Mutex<Vec<CastSessionSnapshot>>>,
    Arc<dyn crayon_cast_adapter::CastSessionListener>,
) {
    let events: Arc<Mutex<Vec<CastSessionSnapshot>>> = Arc::new(Mutex::new(Vec::new()));
    let moved = Arc::clone(&events);
    let listener = Arc::new(move |snapshot: CastSessionSnapshot| {
        moved.lock().unwrap().push(snapshot);
    });
    (events, listener)
}

fn fake_with_device() -> FakeCastFacade {
    let fake = FakeCastFacade::new();
    fake.upsert_device(discovered("dev-01", "Living Room TV", DeviceState::Ready));
    fake
}

/// CS-001: Fake facade start/refresh/stop discovery — lifecycle idempotent,
/// UI consumes device snapshots only.
#[test]
fn cs_001_discovery_lifecycle_idempotent_snapshot_only() {
    let fake = fake_with_device();
    fake.start_discovery().expect("start");
    fake.start_discovery()
        .expect("repeated start is idempotent");
    fake.refresh_discovery().expect("refresh");
    fake.stop_discovery().expect("stop");
    fake.stop_discovery().expect("repeated stop is a no-op");
    // UI reads a snapshot: stable ids, friendly name, state — no locator.
    let snapshot = fake.list_devices();
    assert_eq!(snapshot.len(), 1);
    assert_eq!(snapshot[0].device_id(), &device("dev-01"));
    let json = serde_json::to_string(&snapshot[0]).expect("device serializes");
    for forbidden in ["ip", "host", "port", "location", "udn", "url"] {
        assert!(
            !json.to_ascii_lowercase().contains(forbidden),
            "device snapshot carries no network locator key: {forbidden}"
        );
    }
}

/// CS-002: same-name devices, UDN conflict and multi-interface re-announce —
/// consumers use the stable device ID and never cache an IP.
#[test]
fn cs_002_same_name_udn_conflict_multi_interface_keep_stable_ids() {
    let fake = FakeCastFacade::new();
    // Two physically distinct receivers report the same friendly name.
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Ready));
    fake.upsert_device(discovered("dev-02", "TV", DeviceState::Ready));
    // UDN conflict: the conflicting announcement resolves to the same stable
    // id and replaces the entry instead of duplicating it.
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Incomplete));
    // Multi-interface: the same receiver re-announced via another NIC keeps
    // its identity (adapter derives the id from the SDK stable device key).
    fake.upsert_device(discovered("dev-01", "TV", DeviceState::Ready));
    let devices = fake.list_devices();
    assert_eq!(
        devices.len(),
        2,
        "no duplicates from conflicts or re-announce"
    );
    let ids: Vec<&DeviceId> = devices.iter().map(DiscoveredDevice::device_id).collect();
    assert!(ids.contains(&&device("dev-01")));
    assert!(ids.contains(&&device("dev-02")));
    assert!(
        devices.iter().all(|d| d.friendly_name() == "TV"),
        "same display name does not merge identities"
    );
}

/// CS-003: six-character cast code — success, format error, not found,
/// expired, cancellation. The browser never reimplements the codec.
#[test]
fn cs_003_cast_code_branches_map_to_stable_outcomes() {
    let fake = fake_with_device();
    let code = CastCode::new("7K2-9Q4").expect("normalized six-char code");
    fake.bind_cast_code(&code, &device("dev-01"));

    // Success.
    let resolved = fake
        .resolve_device_by_cast_code(&code)
        .expect("bound code resolves");
    assert_eq!(resolved.device_id(), &device("dev-01"));

    // Format error: rejected at the boundary before any facade call.
    assert_eq!(CastCode::new("short"), Err(CastError::InvalidCastCode));
    assert_eq!(CastCode::new("12 45"), Err(CastError::InvalidCastCode));
    assert_eq!(CastCode::new("ABCDE!"), Err(CastError::InvalidCastCode));

    // Not found: valid format, no receiver answers.
    let unbound = CastCode::new("ZZZ999").expect("valid code");
    assert_eq!(
        fake.resolve_device_by_cast_code(&unbound),
        Err(CastError::DeviceNotFound)
    );

    // Expired: the code was bound but the receiver is gone — the same stable
    // "not found" outcome as an unanswered code (finalized in SDK-07).
    fake.remove_device(&device("dev-01"));
    assert_eq!(
        fake.resolve_device_by_cast_code(&code),
        Err(CastError::DeviceNotFound),
        "expired code maps to DeviceNotFound"
    );
    fake.upsert_device(discovered("dev-01", "Living Room TV", DeviceState::Ready));

    // Error mid-resolution: LAN failure surfaces as a stable code.
    fake.fail_next_resolve_cast_code(CastError::NetworkUnavailable);
    assert_eq!(
        fake.resolve_device_by_cast_code(&code),
        Err(CastError::NetworkUnavailable)
    );

    // Cancellation (finalized in SDK-07): the pinned SDK revision has no
    // cooperative cancel API on `resolve_device_by_cast_code`; the call is
    // bounded and cancel is caller-side abandonment — a late result is
    // discarded and no facade error surfaces. The scripted one-shot error
    // pins that any mid-flight abort the product layer reports is a stable
    // code, never an SDK string.
    fake.fail_next_resolve_cast_code(CastError::InvalidState);
    assert_eq!(
        fake.resolve_device_by_cast_code(&code),
        Err(CastError::InvalidState),
        "caller-side abort surfaces as a stable code, never an SDK string"
    );
    // One-shot scripting: the code resolves again afterwards.
    assert!(fake.resolve_device_by_cast_code(&code).is_ok());
}

/// CS-003 (connection side, finalized in SDK-07): connect/disconnect state
/// mapping — success, idempotent repeat, device switch, unknown and aged-out
/// devices, route lost, disconnect and reconnect.
#[test]
fn cs_003_connect_disconnect_state_mapping() {
    let fake = fake_with_device();
    let first = device("dev-01");

    // Unknown device.
    assert_eq!(
        fake.connect(&device("dev-99")),
        Err(CastError::DeviceNotFound)
    );

    // Aged-out device: absent from the snapshot, reported as not found.
    fake.upsert_device(discovered("dev-02", "Kitchen", DeviceState::Stale));
    assert_eq!(
        fake.connect(&device("dev-02")),
        Err(CastError::DeviceNotFound)
    );

    // Success, then an idempotent repeat.
    fake.connect(&first).expect("connect");
    fake.connect(&first)
        .expect("repeated connect to the same device is idempotent");
    assert_eq!(fake.connected_device(), Some(first.clone()));

    // Visible device with an expired validated route: RouteLost, scripted
    // one-shot; the current connection is not dropped by the failed attempt.
    fake.fail_next_connect(CastError::RouteLost);
    assert_eq!(fake.connect(&first), Err(CastError::RouteLost));
    assert_eq!(fake.connected_device(), Some(first.clone()));

    // Connecting another device switches.
    fake.upsert_device(discovered("dev-02", "Kitchen", DeviceState::Ready));
    fake.connect(&device("dev-02")).expect("switch");
    assert_eq!(fake.connected_device(), Some(device("dev-02")));

    // Disconnect is an idempotent no-op and reconnect works afterwards.
    fake.disconnect();
    fake.disconnect();
    assert_eq!(fake.connected_device(), None);
    fake.connect(&first).expect("reconnect after disconnect");
    assert_eq!(fake.connected_device(), Some(first));
}

/// CS-004: receiver capability change — the facade answers with the latest
/// assessment; an older cached result is stale.
#[test]
fn cs_004_capability_change_invalidates_older_assessment() {
    let fake = fake_with_device();
    fake.set_assessment(
        &device("dev-01"),
        CastMediaKind::Hls,
        AssessmentStatus::Supported,
    );
    let before = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Hls)
        .expect("first assessment");
    assert_eq!(before.status(), AssessmentStatus::Supported);

    // Receiver firmware/profile changes: the same query now reports a
    // different point-in-time fact.
    fake.set_assessment(
        &device("dev-01"),
        CastMediaKind::Hls,
        AssessmentStatus::Unsupported,
    );
    let after = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Hls)
        .expect("second assessment");
    assert_eq!(after.status(), AssessmentStatus::Unsupported);
    assert_ne!(before, after, "policy must not reuse the stale cache entry");

    // Unknown receiver profile fails closed.
    let unknown = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Video)
        .expect("unscripted assessment");
    assert_eq!(unknown.status(), AssessmentStatus::Unknown);
}

/// CS-005: Direct plan delivery goes only through the facade; the URL is
/// forwarded unchanged and failures surface as stable codes.
#[test]
fn cs_005_direct_delivery_via_facade_only() {
    let fake = fake_with_device();
    fake.connect(&device("dev-01")).expect("connect");
    let request = direct_mp4_request("dev-01");
    let session = fake.cast_media(&request).expect("delivery starts");
    assert_eq!(
        session.generation(),
        crayon_domain::SessionGeneration::INITIAL
    );
    assert_eq!(
        fake.calls().last(),
        Some(&FakeCall::CastMedia {
            device: device("dev-01"),
            protocol: DeliveryProtocol::Mp4,
            url: "https://cdn.example/v/film.mp4?sig=example".to_owned(),
        }),
        "facade received the planned URL byte-for-byte; no descriptor assembly"
    );

    // Delivery to a device other than the connected one fails closed.
    fake.upsert_device(discovered("dev-02", "Kitchen", DeviceState::Ready));
    assert_eq!(
        fake.cast_media(&direct_mp4_request("dev-02")),
        Err(CastError::InvalidState)
    );

    // Receiver-side start failure is a stable code, not an SDK message.
    fake.fail_next_cast_media(CastError::UnsupportedByReceiver);
    assert_eq!(
        fake.cast_media(&direct_mp4_request("dev-01")),
        Err(CastError::UnsupportedByReceiver)
    );
}

/// CS-006: pause/seek/volume/stop with a stale session handle — the adapter
/// rejects old generations before they reach the receiver.
#[test]
fn cs_006_stale_session_handle_rejected_on_all_controls() {
    let fake = fake_with_device();
    fake.connect(&device("dev-01")).expect("connect");
    let first = fake
        .cast_media(&direct_mp4_request("dev-01"))
        .expect("first cast");
    let second = fake
        .cast_media(&direct_mp4_request("dev-01"))
        .expect("replacement cast");
    assert!(second.generation().supersedes(first.generation()));

    let recorded_before = fake.calls().len();
    assert_eq!(fake.play(&first), Err(CastError::StaleSessionGeneration));
    assert_eq!(fake.pause(&first), Err(CastError::StaleSessionGeneration));
    assert_eq!(
        fake.seek(&first, 10),
        Err(CastError::StaleSessionGeneration)
    );
    assert_eq!(
        fake.set_volume(&first, Volume::new(10).expect("volume")),
        Err(CastError::StaleSessionGeneration)
    );
    assert_eq!(
        fake.set_muted(&first, true),
        Err(CastError::StaleSessionGeneration)
    );
    assert_eq!(fake.stop(&first), Err(CastError::StaleSessionGeneration));
    assert_eq!(
        fake.playback_position(&first),
        Err(CastError::StaleSessionGeneration)
    );
    assert_eq!(
        fake.calls().len(),
        recorded_before,
        "no stale control reached the receiver"
    );

    // The current handle keeps working.
    fake.pause(&second).expect("current generation accepted");
    assert_eq!(
        fake.current_session().expect("snapshot").playback(),
        CastPlaybackState::Paused
    );
}

/// CS-007: natural end, receiver-side stop and route lost converge to a
/// terminal state with a stable reason; stale events cannot stop a newer
/// session; external handoff creates no SDK session.
#[test]
fn cs_007_terminal_events_converge_and_stale_events_are_fenced() {
    for (drive, reason) in [
        (
            FakeCastFacade::simulate_natural_end as fn(&FakeCastFacade),
            CastTerminalReason::EndedNormally,
        ),
        (
            FakeCastFacade::simulate_receiver_stop,
            CastTerminalReason::StoppedByReceiver,
        ),
        (
            FakeCastFacade::simulate_route_lost,
            CastTerminalReason::ReceiverUnreachable,
        ),
    ] {
        let fake = fake_with_device();
        fake.connect(&device("dev-01")).expect("connect");
        let (events, listener) = collect_events();
        let _subscription = fake.subscribe_session_events(listener, false);
        fake.cast_media(&direct_mp4_request("dev-01"))
            .expect("cast");
        drive(&fake);
        let terminal = fake
            .current_session()
            .expect("snapshot kept after terminal");
        assert!(terminal.is_terminal(), "state converges to terminated");
        assert_eq!(terminal.terminal_reason(), Some(reason));
        assert_eq!(
            events
                .lock()
                .unwrap()
                .last()
                .expect("terminal event")
                .terminal_reason(),
            Some(reason),
            "listeners observe the terminal transition ({reason:?})"
        );
    }

    // An old-generation event must never stop the newer session.
    let fake = fake_with_device();
    fake.connect(&device("dev-01")).expect("connect");
    let first = fake
        .cast_media(&direct_mp4_request("dev-01"))
        .expect("first cast");
    let second = fake
        .cast_media(&direct_mp4_request("dev-01"))
        .expect("second cast");
    fake.push_session_snapshot(CastSessionSnapshot::new(
        first,
        CastSessionPhase::Terminated,
        CastPlaybackState::Failed,
        42,
        Some(CastTerminalReason::ReceiverUnreachable),
    ));
    let current = fake.current_session().expect("current session");
    assert_eq!(current.session(), &second);
    assert!(
        !current.is_terminal(),
        "stale event did not stop the new session"
    );

    // External-client handoff (PL-015/MED-19) is not expressible: without a
    // Direct/Relay route there is simply no session.
    let handoff = FakeCastFacade::new();
    assert_eq!(handoff.current_session(), None);
    assert_eq!(
        handoff.cast_media(&direct_mp4_request("dev-01")),
        Err(CastError::InvalidState),
        "no connected device -> fail closed, never a handoff session"
    );
}

/// CS-008: SDK unsupported/protocol/permission-style failures surface as
/// stable product error codes; callers match variants/codes, never strings.
#[test]
fn cs_008_scripted_failures_surface_only_stable_codes() {
    let fake = fake_with_device();
    fake.connect(&device("dev-01")).expect("connect");
    let session = fake
        .cast_media(&direct_mp4_request("dev-01"))
        .expect("cast");

    for scripted in [
        CastError::UnsupportedByReceiver,
        CastError::ReceiverProtocol,
        CastError::ReceiverUnreachable,
        CastError::RouteLost,
        CastError::Internal,
    ] {
        fake.fail_next_control(scripted);
        let error = fake.pause(&session).expect_err("scripted failure surfaces");
        assert_eq!(error, scripted);
        // The wire form is the stable snake_case code, resolvable back to the
        // same variant — no natural-language detail anywhere.
        assert_eq!(CastError::from_code(error.code()), Some(error));
        assert_eq!(error.to_string(), error.code());
    }
    // Scripting is one-shot: the next control succeeds again.
    fake.pause(&session).expect("recovered");
}

/// CS-009: behavior golden for Cast-SDK revision upgrades — a full lifecycle
/// (discovery -> connect -> assess -> cast -> control -> terminal) pinned to
/// an exact call and event sequence. An upgrade that changes facade
/// semantics breaks this golden instead of drifting silently.
#[test]
fn cs_009_full_lifecycle_behavior_golden() {
    let fake = fake_with_device();
    let (events, listener) = collect_events();
    let _subscription = fake.subscribe_session_events(listener, false);

    fake.start_discovery().expect("start discovery");
    fake.refresh_discovery().expect("refresh");
    fake.connect(&device("dev-01")).expect("connect");
    fake.set_assessment(
        &device("dev-01"),
        CastMediaKind::Video,
        AssessmentStatus::Supported,
    );
    let assessment = fake
        .assess_receiver(&device("dev-01"), CastMediaKind::Video)
        .expect("assessment");
    assert_eq!(assessment.status(), AssessmentStatus::Supported);

    let session = fake
        .cast_media(&direct_mp4_request("dev-01"))
        .expect("cast");
    fake.pause(&session).expect("pause");
    fake.play(&session).expect("play");
    fake.seek(&session, 30).expect("seek");
    fake.simulate_natural_end();
    fake.stop(&session)
        .expect("stop after terminal is idempotent");
    fake.disconnect();
    fake.stop_discovery().expect("stop discovery");

    let calls = fake.calls();
    assert_eq!(
        calls,
        vec![
            FakeCall::StartDiscovery,
            FakeCall::RefreshDiscovery,
            FakeCall::Connect(device("dev-01")),
            FakeCall::AssessReceiver(device("dev-01"), CastMediaKind::Video),
            FakeCall::CastMedia {
                device: device("dev-01"),
                protocol: DeliveryProtocol::Mp4,
                url: "https://cdn.example/v/film.mp4?sig=example".to_owned(),
            },
            FakeCall::Pause(session.clone()),
            FakeCall::Play(session.clone()),
            FakeCall::Seek {
                session: session.clone(),
                position_seconds: 30,
            },
            FakeCall::Disconnect,
            FakeCall::StopDiscovery,
        ],
        "golden call sequence"
    );

    let observed: Vec<(
        CastSessionPhase,
        CastPlaybackState,
        u64,
        Option<CastTerminalReason>,
    )> = events
        .lock()
        .unwrap()
        .iter()
        .map(|event| {
            (
                event.phase(),
                event.playback(),
                event.state_revision(),
                event.terminal_reason(),
            )
        })
        .collect();
    assert_eq!(
        observed,
        vec![
            (
                CastSessionPhase::Starting,
                CastPlaybackState::Preparing,
                0,
                None
            ),
            (
                CastSessionPhase::Active,
                CastPlaybackState::Playing,
                1,
                None
            ),
            (CastSessionPhase::Active, CastPlaybackState::Paused, 2, None),
            (
                CastSessionPhase::Active,
                CastPlaybackState::Playing,
                3,
                None
            ),
            (
                CastSessionPhase::Active,
                CastPlaybackState::Playing,
                4,
                None
            ),
            (
                CastSessionPhase::Terminated,
                CastPlaybackState::Ended,
                5,
                Some(CastTerminalReason::EndedNormally),
            ),
        ],
        "golden supervision sequence"
    );
}
