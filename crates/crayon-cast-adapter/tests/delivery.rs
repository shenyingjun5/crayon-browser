//! CS-005 delivery execution (SDK-09): a planned Direct/HLS/Relay URL goes
//! only through the `CastFacade` — deterministically orchestrated against the
//! scripted `FakeCastFacade` (test-support), asserting the exact call
//! sequence and arguments. Test names keep the case ID.

use crayon_cast_adapter::{
    deliver, CastError, CastFacade, CastMediaUrl, CastSessionSnapshot, DeliveryProtocol,
    DeliveryRoute, DeviceState, DiscoveredDevice, PlannedDelivery,
};
use crayon_domain::{DeviceId, SessionGeneration};
use std::sync::{Arc, Mutex};
use test_support::cast_facade::{FakeCall, FakeCastFacade};

fn device(id: &str) -> DeviceId {
    DeviceId::new(id).expect("test device id")
}

fn planned(
    id: &str,
    route: DeliveryRoute,
    protocol: DeliveryProtocol,
    url: &str,
) -> PlannedDelivery {
    PlannedDelivery::new(
        device(id),
        route,
        protocol,
        CastMediaUrl::new(url).expect("test url"),
    )
}

fn connected_fake() -> FakeCastFacade {
    let fake = FakeCastFacade::new();
    fake.upsert_device(DiscoveredDevice::new(
        device("dev-01"),
        "Living Room TV".to_owned(),
        DeviceState::Ready,
        false,
    ));
    fake.connect(&device("dev-01")).expect("connect");
    fake
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

/// CS-005 (Direct MP4): the executor issues exactly one `cast_media` with the
/// planned URL forwarded byte-for-byte — no descriptor assembly, no
/// capability re-check, no follow-up `play` (a successful cast plays).
#[test]
fn cs_005_direct_mp4_delivery_goes_only_through_facade() {
    let fake = connected_fake();
    let (events, listener) = collect_events();
    let _subscription = fake.subscribe_session_events(listener, false);

    let order = planned(
        "dev-01",
        DeliveryRoute::Direct,
        DeliveryProtocol::Mp4,
        "https://cdn.example/v/film.mp4?sig=example",
    );
    let session = deliver(&fake, &order).expect("delivery starts");
    assert_eq!(session.generation(), SessionGeneration::INITIAL);

    assert_eq!(
        fake.calls(),
        vec![
            FakeCall::Connect(device("dev-01")),
            FakeCall::CastMedia {
                device: device("dev-01"),
                protocol: DeliveryProtocol::Mp4,
                url: "https://cdn.example/v/film.mp4?sig=example".to_owned(),
            },
        ],
        "exactly one facade delivery call; no assess, no play, nothing else"
    );

    // Delivery success implies Playing on the receiver (Starting -> Active).
    let observed: Vec<_> = events
        .lock()
        .unwrap()
        .iter()
        .map(|event| (event.phase(), event.playback()))
        .collect();
    assert_eq!(
        observed,
        vec![
            (
                crayon_cast_adapter::CastSessionPhase::Starting,
                crayon_cast_adapter::CastPlaybackState::Preparing,
            ),
            (
                crayon_cast_adapter::CastSessionPhase::Active,
                crayon_cast_adapter::CastPlaybackState::Playing,
            ),
        ],
        "cast start alone reaches Playing; no play call is needed"
    );
}

/// CS-005 (Direct HLS): the HLS branch comes from the policy candidate
/// protocol, not from URL sniffing — an HLS plan without an `.m3u8` suffix
/// still maps to the HLS delivery branch.
#[test]
fn cs_005_direct_hls_branch_comes_from_plan_not_url() {
    let fake = connected_fake();
    let order = planned(
        "dev-01",
        DeliveryRoute::Direct,
        DeliveryProtocol::Hls,
        "https://cdn.example/live/channel",
    );
    deliver(&fake, &order).expect("delivery starts");
    assert_eq!(
        fake.calls().last(),
        Some(&FakeCall::CastMedia {
            device: device("dev-01"),
            protocol: DeliveryProtocol::Hls,
            url: "https://cdn.example/live/channel".to_owned(),
        }),
        "protocol carried by the plan selects the HLS branch"
    );
}

/// CS-005 (Relay): the opaque relay URL is forwarded unchanged and the route
/// changes nothing on the SDK call — the relay serves the upstream protocol
/// (MP4 or HLS) as-is.
#[test]
fn cs_005_relay_url_forwarded_byte_for_byte() {
    let relay_hls = "http://127.0.0.1:18932/s/0123456789abcdef0123456789abcdef/master.m3u8";
    let relay_mp4 = "http://127.0.0.1:18932/s/fedcba9876543210fedcba9876543210/r/main/movie.mp4";
    for (protocol, url) in [
        (DeliveryProtocol::Hls, relay_hls),
        (DeliveryProtocol::Mp4, relay_mp4),
    ] {
        let fake = connected_fake();
        let order = planned("dev-01", DeliveryRoute::Relay, protocol, url);
        deliver(&fake, &order).expect("relay delivery starts");
        assert_eq!(
            fake.calls().last(),
            Some(&FakeCall::CastMedia {
                device: device("dev-01"),
                protocol,
                url: url.to_owned(),
            }),
            "relay token URL forwarded byte-for-byte ({protocol:?})"
        );
    }
}

/// CS-005 (stale plan): the planned device must still be the connected one —
/// a device switch or a lost connection fails closed with `InvalidState`
/// (SessionExpired semantics) before any receiver traffic.
#[test]
fn cs_005_stale_plan_device_mismatch_fails_closed() {
    let fake = FakeCastFacade::new();
    for id in ["dev-01", "dev-02"] {
        fake.upsert_device(DiscoveredDevice::new(
            device(id),
            "TV".to_owned(),
            DeviceState::Ready,
            false,
        ));
    }

    // No connection at all.
    let order = planned(
        "dev-01",
        DeliveryRoute::Direct,
        DeliveryProtocol::Mp4,
        "https://cdn.example/v/film.mp4",
    );
    assert_eq!(deliver(&fake, &order), Err(CastError::InvalidState));

    // Connected to another device: the plan bound to dev-01 is stale.
    fake.connect(&device("dev-02")).expect("connect other");
    assert_eq!(
        deliver(&fake, &order),
        Err(CastError::InvalidState),
        "planned device != connected device: stale plan rejected"
    );

    assert_eq!(
        fake.calls(),
        vec![FakeCall::Connect(device("dev-02"))],
        "no delivery call ever reached the receiver"
    );
}

/// CS-005 (failure semantics): a receiver-side failure propagates as its
/// stable code — unsupported stays explicit — with exactly one attempt and no
/// retry, upgrade or reinterpretation.
#[test]
fn cs_005_failures_propagate_stably_without_retry() {
    let fake = connected_fake();
    let order = planned(
        "dev-01",
        DeliveryRoute::Direct,
        DeliveryProtocol::Mp4,
        "https://cdn.example/v/film.mp4",
    );

    let attempts = |fake: &FakeCastFacade| {
        fake.calls()
            .iter()
            .filter(|call| matches!(call, FakeCall::CastMedia { .. }))
            .count()
    };
    for scripted in [
        CastError::UnsupportedByReceiver,
        CastError::ReceiverProtocol,
        CastError::CastStartFailed,
    ] {
        let before = attempts(&fake);
        fake.fail_next_cast_media(scripted);
        assert_eq!(deliver(&fake, &order), Err(scripted));
        assert_eq!(
            attempts(&fake),
            before + 1,
            "one attempt only, no retry ({scripted:?})"
        );
        // One-shot scripting consumed: the next attempt succeeds again, so
        // each iteration starts from a clean slate.
        deliver(&fake, &order).expect("recovery after one-shot failure");
    }
}

/// CS-005 (replacement): a repeated delivery replaces the previous session —
/// the old session reports `ReplacedByNewCast` through supervision, the new
/// generation supersedes, and no `stop` is issued beforehand.
#[test]
fn cs_005_repeat_delivery_replaces_previous_session() {
    let fake = connected_fake();
    let (events, listener) = collect_events();
    let _subscription = fake.subscribe_session_events(listener, false);

    let first = deliver(
        &fake,
        &planned(
            "dev-01",
            DeliveryRoute::Direct,
            DeliveryProtocol::Mp4,
            "https://cdn.example/v/one.mp4",
        ),
    )
    .expect("first delivery");
    let second = deliver(
        &fake,
        &planned(
            "dev-01",
            DeliveryRoute::Direct,
            DeliveryProtocol::Mp4,
            "https://cdn.example/v/two.mp4",
        ),
    )
    .expect("second delivery replaces the first");

    assert!(second.generation().supersedes(first.generation()));
    assert_eq!(
        fake.calls()
            .iter()
            .filter(|call| matches!(call, FakeCall::CastMedia { .. }))
            .count(),
        2,
        "two deliveries, no stop in between"
    );

    let observed = events.lock().unwrap();
    let replacement = observed
        .iter()
        .find(|event| {
            event.session() == &first
                && event.terminal_reason()
                    == Some(crayon_cast_adapter::CastTerminalReason::ReplacedByNewCast)
        })
        .expect("old session reports ReplacedByNewCast");
    assert!(replacement.is_terminal());
    assert_eq!(
        fake.current_session().expect("current session").session(),
        &second,
        "supervision converged on the replacement session"
    );
}
