//! Fake E2E V2 (SDK-12): local-fixture full chain — eligible playback →
//! select fake receiver → capability → policy Direct/Relay → connect →
//! deliver → session-bound control → terminal convergence → relay revoke /
//! capability invalidation / handle retirement. Every terminal path asserts
//! resource cleanup; error/boundary coverage: connect failure, plan
//! Reject/Handoff (PL-014/PL-015), single-step downgrade (MED-17), stale
//! generation fencing (CS-006/CS-007), device switch voiding the old plan
//! (PL-012) and idempotent repeated stop.

use crayon_app_runtime::cast_usecase::{CastPhase, CastStartOutcome, CastUsecase, RelayRevocation};
use crayon_app_runtime::delivery::{CoreSessionBackend, DeliveryRequest};
use crayon_cast_adapter::{
    AssessmentStatus, CastError, CastFacade, CastMediaKind, CastPlaybackState, CastSessionPhase,
    CastSessionRef, CastTerminalReason, DeviceState, DiscoveredDevice, ReceiverCapabilityCache,
    Volume,
};
use crayon_cast_policy::HandoffAvailability;
use crayon_domain::{CoreError, DeviceId, ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyInput, HandoffConfirmation, HandoffReason, HeadersClass,
    MediaCandidate, PageContext, PlaybackState, ProtocolKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;
use crayon_relay::runtime::{RelayRuntime, RelayRuntimeConfig};
use crayon_relay::session::RevokeReason;
use std::sync::Arc;
use test_support::cast_facade::{FakeCall, FakeCastFacade};
use test_support::upstream::{MockUpstream, UpstreamScript};

const MOVIE_BODY: &[u8] = b"0123456789";

struct E2e {
    facade: Arc<FakeCastFacade>,
    cache: Arc<ReceiverCapabilityCache>,
    runtime: Arc<RelayRuntime>,
    usecase: CastUsecase,
    device: DeviceId,
}

fn device(id: &str) -> DeviceId {
    DeviceId::new(id).unwrap()
}

fn register_device(facade: &FakeCastFacade, id: &str) -> DeviceId {
    let device = device(id);
    facade.upsert_device(DiscoveredDevice::new(
        device.clone(),
        format!("Receiver {id}"),
        DeviceState::Ready,
        false,
    ));
    facade.set_assessment(&device, CastMediaKind::Video, AssessmentStatus::Supported);
    facade.set_assessment(&device, CastMediaKind::Hls, AssessmentStatus::Supported);
    device
}

/// Assembles the usecase over the fake facade, the real capability cache,
/// the real relay runtime/session backend and the real revocation wiring
/// (`RelayRuntime` implements `RelayRevocation`).
async fn e2e() -> E2e {
    let facade = Arc::new(FakeCastFacade::new());
    let device = register_device(&facade, "dev-01");
    let runtime = RelayRuntime::start(RelayRuntimeConfig {
        media_host: "127.0.0.1".to_string(),
        allow_private_upstreams: true,
        ..RelayRuntimeConfig::default()
    })
    .await
    .unwrap();
    let facade_trait: Arc<dyn CastFacade> = facade.clone();
    let cache = Arc::new(ReceiverCapabilityCache::new(
        facade_trait,
        Default::default(),
    ));
    let backend = CoreSessionBackend::new(runtime.core().clone(), runtime.media_base_url());
    let revocation: Arc<dyn RelayRevocation> = runtime.clone();
    let usecase = CastUsecase::new(facade.clone(), cache.clone(), Box::new(backend), revocation);
    E2e {
        facade,
        cache,
        runtime,
        usecase,
        device,
    }
}

fn request(device: &DeviceId, candidate_url: &str, headers: HeadersClass) -> DeliveryRequest {
    DeliveryRequest {
        input: CastPolicyInput::new(
            PageContext::new(
                TabId::new("tab-01").unwrap(),
                "https://example.com/watch".to_string(),
            ),
            PlaybackState::new(120.0, Some(3600.0), false),
            MediaCandidate::new(
                candidate_url.to_string(),
                ProtocolKind::Mp4,
                false,
                headers,
                None,
                None,
                AdContinuity::Preserved,
            ),
            ReceiverCapabilities::new(true, true, true, true, true, true, 2160),
        ),
        observation: PlaybackObservation::new(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
        ),
        protection: Protection::Clear,
        external_client_handoff: HandoffAvailability::Available,
        receiver: device.clone(),
        receiver_ip: Some(std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST)),
    }
}

/// Drives the UI flow up to the picker.
fn open_picker(e2e: &E2e) {
    e2e.usecase.on_page_browsing();
    e2e.usecase.on_playback_eligible();
    e2e.usecase.open_receiver_picker().unwrap();
}

/// Starts a relay cast of the upstream MP4 and drains the startup events.
fn start_relay_cast(e2e: &E2e, upstream: &MockUpstream, device: &DeviceId) -> CastSessionRef {
    let outcome = e2e.usecase.start_cast(&request(
        device,
        &upstream.url("/movie.mp4"),
        HeadersClass::RefererOnly,
    ));
    let CastStartOutcome::Casting(session) = outcome else {
        panic!("expected casting: {outcome:?}")
    };
    assert_eq!(e2e.usecase.phase(), CastPhase::Casting);
    e2e.usecase.drain_session_events();
    session
}

/// The media URL the facade was last asked to cast (verbatim record).
fn casted_url(facade: &FakeCastFacade) -> String {
    facade
        .calls()
        .iter()
        .rev()
        .find_map(|call| match call {
            FakeCall::CastMedia { url, .. } => Some(url.clone()),
            _ => None,
        })
        .expect("a CastMedia call was recorded")
}

fn count_calls(facade: &FakeCastFacade, pred: impl Fn(&FakeCall) -> bool) -> usize {
    facade.calls().iter().filter(|call| pred(call)).count()
}

fn assess_calls(facade: &FakeCastFacade, device: &DeviceId) -> usize {
    count_calls(
        facade,
        |call| matches!(call, FakeCall::AssessReceiver(id, _) if id == device),
    )
}

async fn get_status(url: &str) -> reqwest::StatusCode {
    reqwest::Client::new()
        .get(url)
        .send()
        .await
        .unwrap()
        .status()
}

#[tokio::test]
async fn e2e_002_fake_relay_full_chain_user_stop_cleanup() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    open_picker(&e2e);
    let session = start_relay_cast(&e2e, &upstream, &e2e.device);
    assert_eq!(e2e.usecase.active_session(), Some(session.clone()));

    // The receiver-facing URL is the opaque relay token URL; the fake
    // receiver pulls a range through it (E2E-002 fake semantics).
    let media_url = casted_url(&e2e.facade);
    assert!(media_url.contains("/s/"), "{media_url}");
    let resp = reqwest::Client::new()
        .get(&media_url)
        .header("Range", "bytes=0-3")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"0123");

    // Session-bound controls converge through supervision events.
    e2e.usecase.pause().unwrap();
    e2e.usecase.seek(30).unwrap();
    e2e.usecase.set_volume(Volume::new(80).unwrap()).unwrap();
    e2e.usecase.play().unwrap();
    e2e.usecase.drain_session_events();
    assert_eq!(
        e2e.usecase.observed_session().unwrap().playback(),
        CastPlaybackState::Playing
    );

    // User stop: idempotent, exactly one receiver Stop, convergence and
    // cleanup happen at drain time (never inside the listener callback).
    let assess_before = assess_calls(&e2e.facade, &e2e.device);
    e2e.usecase.stop_cast().unwrap();
    assert_eq!(e2e.usecase.phase(), CastPhase::Stopping);
    e2e.usecase.stop_cast().unwrap();
    assert_eq!(
        count_calls(&e2e.facade, |call| matches!(call, FakeCall::Stop(_))),
        1,
        "repeated stop reaches the receiver once"
    );
    let stats = e2e.usecase.drain_session_events();
    assert_eq!(stats.terminal_converged, 1);
    assert_eq!(e2e.usecase.phase(), CastPhase::SelectingReceiver);
    assert_eq!(e2e.usecase.active_session(), None);

    // Terminal cleanup: relay token revoked, capability entry invalidated,
    // old handle fenced by the facade terminal matrix.
    assert_eq!(get_status(&media_url).await, 401);
    e2e.cache.capabilities(&e2e.device).unwrap();
    assert_eq!(
        assess_calls(&e2e.facade, &e2e.device),
        assess_before + 2,
        "invalidated capability entry is re-assessed"
    );
    assert_eq!(e2e.facade.pause(&session), Err(CastError::NoActiveSession));
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn cs_007_terminal_paths_converge_with_resource_cleanup() {
    type Scenario = (
        &'static str,
        Box<dyn Fn(&FakeCastFacade)>,
        CastPlaybackState,
        CastTerminalReason,
    );
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    let scenarios: Vec<Scenario> = vec![
        (
            "natural end",
            Box::new(|facade| facade.simulate_natural_end()),
            CastPlaybackState::Ended,
            CastTerminalReason::EndedNormally,
        ),
        (
            "receiver stop",
            Box::new(|facade| facade.simulate_receiver_stop()),
            CastPlaybackState::Stopped,
            CastTerminalReason::StoppedByReceiver,
        ),
        (
            "route lost",
            Box::new(|facade| facade.simulate_route_lost()),
            CastPlaybackState::Stopped, // route lost maps to Stopped, never Failed
            CastTerminalReason::ReceiverUnreachable,
        ),
        (
            "replaced by other controller",
            Box::new(|facade| facade.simulate_replaced_by_other_controller()),
            CastPlaybackState::Stopped,
            CastTerminalReason::ReplacedByOtherController,
        ),
    ];
    for (name, trigger, playback, reason) in scenarios {
        open_picker(&e2e);
        let _session = start_relay_cast(&e2e, &upstream, &e2e.device);
        let media_url = casted_url(&e2e.facade);
        assert_eq!(get_status(&media_url).await, 200, "{name}: token live");

        trigger(&e2e.facade);
        // Terminal triple (phase, playback, reason) as supervised facts.
        let terminal = e2e.facade.current_session().unwrap();
        assert_eq!(terminal.phase(), CastSessionPhase::Terminated, "{name}");
        assert_eq!(terminal.playback(), playback, "{name}");
        assert_eq!(terminal.terminal_reason(), Some(reason), "{name}");

        let assess_before = assess_calls(&e2e.facade, &e2e.device);
        let stats = e2e.usecase.drain_session_events();
        assert_eq!(stats.terminal_converged, 1, "{name}");
        assert_eq!(e2e.usecase.phase(), CastPhase::SelectingReceiver, "{name}");
        assert_eq!(e2e.usecase.active_session(), None, "{name}");
        assert_eq!(get_status(&media_url).await, 401, "{name}: relay revoked");
        e2e.cache.capabilities(&e2e.device).unwrap();
        assert_eq!(
            assess_calls(&e2e.facade, &e2e.device),
            assess_before + 2,
            "{name}: capability invalidated"
        );
    }
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn connect_failure_is_plain_and_revokes_the_planned_relay_session() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    open_picker(&e2e);
    e2e.facade.fail_next_connect(CastError::RouteLost);
    let outcome = e2e.usecase.start_cast(&request(
        &e2e.device,
        &upstream.url("/movie.mp4"),
        HeadersClass::RefererOnly,
    ));
    // PL-014: plain failure — no downgrade, no retry, no receiver traffic.
    assert_eq!(outcome, CastStartOutcome::Failed(CastError::RouteLost));
    assert_eq!(e2e.usecase.phase(), CastPhase::Failed);
    assert_eq!(
        count_calls(&e2e.facade, |call| matches!(
            call,
            FakeCall::CastMedia { .. }
        )),
        0
    );
    assert_eq!(
        e2e.runtime
            .trigger(RevokeReason::Stopped, Some(&e2e.device)),
        0,
        "the relay session opened during planning was already revoked"
    );
    assert!(e2e.facade.current_session().is_none());
    // Recovery: a fresh attempt is an ordinary new cast.
    let session = start_relay_cast(&e2e, &upstream, &e2e.device);
    assert_eq!(e2e.usecase.active_session(), Some(session));
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn pl_014_015_reject_and_handoff_create_no_session_material() {
    // No upstream fetch happens on these paths; candidates stay remote URLs.
    let e2e = e2e().await;

    // Plan Reject (DRM): plain stable rejection, zero receiver/relay work.
    open_picker(&e2e);
    let mut drm = request(
        &e2e.device,
        "https://cdn.example.com/protected.mp4",
        HeadersClass::None,
    );
    drm.protection = Protection::DrmProtected;
    assert_eq!(
        e2e.usecase.start_cast(&drm),
        CastStartOutcome::Rejected(CoreError::DrmProtected)
    );
    assert_eq!(e2e.usecase.phase(), CastPhase::Failed);
    assert!(
        count_calls(&e2e.facade, |call| matches!(
            call,
            FakeCall::Connect(_) | FakeCall::CastMedia { .. }
        )) == 0
    );
    assert!(e2e.facade.current_session().is_none());

    // Plan Handoff (E2E-004 fake): ad continuity unknown + from-the-start →
    // suggestion only; no SDK session, no relay token, never "casting
    // started"; confirmation stays required (PL-015).
    let mut handoff = request(
        &e2e.device,
        "https://cdn.example.com/master.m3u8",
        HeadersClass::None,
    );
    handoff.input = CastPolicyInput::new(
        handoff.input.page().clone(),
        PlaybackState::new(0.0, Some(3600.0), false),
        MediaCandidate::new(
            "https://cdn.example.com/master.m3u8".to_string(),
            ProtocolKind::Hls,
            false,
            HeadersClass::None,
            None,
            None,
            AdContinuity::Unknown,
        ),
        handoff.input.receiver(),
    );
    let CastStartOutcome::HandoffSuggested(advice) = e2e.usecase.start_cast(&handoff) else {
        panic!("expected handoff suggestion")
    };
    assert_eq!(advice.reason(), HandoffReason::AdContinuityUnknown);
    assert_eq!(advice.confirmation(), HandoffConfirmation::Required);
    assert_eq!(e2e.usecase.phase(), CastPhase::SelectingReceiver);
    assert!(
        count_calls(&e2e.facade, |call| matches!(
            call,
            FakeCall::Connect(_) | FakeCall::CastMedia { .. }
        )) == 0
    );
    assert_eq!(
        e2e.runtime.trigger(RevokeReason::Navigation, None),
        0,
        "no relay session was ever created"
    );
    assert!(e2e.facade.current_session().is_none());
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn deliver_failure_downgrades_exactly_once_without_retry() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    open_picker(&e2e);

    // Handoff available: one failed delivery downgrades once (MED-17).
    e2e.facade
        .fail_next_cast_media(CastError::UnsupportedByReceiver);
    let outcome = e2e.usecase.start_cast(&request(
        &e2e.device,
        &upstream.url("/movie.mp4"),
        HeadersClass::RefererOnly,
    ));
    let CastStartOutcome::HandoffSuggested(advice) = outcome else {
        panic!("expected single-step downgrade: {outcome:?}")
    };
    assert_eq!(advice.reason(), HandoffReason::StartFailed);
    assert_eq!(e2e.usecase.phase(), CastPhase::SelectingReceiver);
    assert_eq!(
        count_calls(&e2e.facade, |call| matches!(
            call,
            FakeCall::CastMedia { .. }
        )),
        1,
        "exactly one delivery attempt, no retry"
    );
    assert_eq!(
        e2e.runtime
            .trigger(RevokeReason::Stopped, Some(&e2e.device)),
        0,
        "the failed attempt's relay session was revoked"
    );
    assert!(e2e.facade.current_session().is_none());

    // No handoff capability: the same failure is a plain stable error.
    let mut plain = request(
        &e2e.device,
        &upstream.url("/movie.mp4"),
        HeadersClass::RefererOnly,
    );
    plain.external_client_handoff = HandoffAvailability::Unavailable;
    e2e.facade
        .fail_next_cast_media(CastError::UnsupportedByReceiver);
    assert_eq!(
        e2e.usecase.start_cast(&plain),
        CastStartOutcome::Failed(CastError::UnsupportedByReceiver)
    );
    assert_eq!(e2e.usecase.phase(), CastPhase::Failed);
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn pl_012_device_switch_voids_old_plan_and_fences_old_session() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    let second = register_device(&e2e.facade, "dev-02");
    open_picker(&e2e);
    let old_session = start_relay_cast(&e2e, &upstream, &e2e.device);
    let old_url = casted_url(&e2e.facade);
    assert_eq!(get_status(&old_url).await, 200);

    // Switching receivers supersedes: old session stopped, its relay
    // sessions revoked, its capability entry invalidated.
    let new_session = start_relay_cast(&e2e, &upstream, &second);
    assert_ne!(old_session, new_session);
    assert!(
        count_calls(
            &e2e.facade,
            |call| matches!(call, FakeCall::Stop(s) if *s == old_session)
        ) >= 1
    );
    assert_eq!(get_status(&old_url).await, 401, "old relay session revoked");
    let assess_before = assess_calls(&e2e.facade, &e2e.device);
    e2e.cache.capabilities(&e2e.device).unwrap();
    assert_eq!(assess_calls(&e2e.facade, &e2e.device), assess_before + 2);

    // The old session's terminal event must not touch the new session.
    let stats = e2e.usecase.drain_session_events();
    assert_eq!(stats.terminal_converged, 0);
    assert_eq!(e2e.usecase.phase(), CastPhase::Casting);
    assert_eq!(e2e.usecase.active_session(), Some(new_session));
    assert_eq!(
        e2e.facade.pause(&old_session),
        Err(CastError::StaleSessionGeneration),
        "old handle fenced by generation"
    );
    let new_url = casted_url(&e2e.facade);
    assert_eq!(get_status(&new_url).await, 200, "new session unaffected");
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn stale_generation_events_never_converge_the_new_session() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    open_picker(&e2e);
    let old_session = start_relay_cast(&e2e, &upstream, &e2e.device);
    // Same-device re-cast: supersedes the old session.
    let new_session = start_relay_cast(&e2e, &upstream, &e2e.device);
    let stats = e2e.usecase.drain_session_events();
    assert_eq!(stats.terminal_converged, 0);
    assert_eq!(e2e.usecase.active_session(), Some(new_session.clone()));

    // The fake delivers an explicit old-generation terminal: fenced away.
    let stale_terminal = crayon_cast_adapter::CastSessionSnapshot::new(
        old_session.clone(),
        CastSessionPhase::Terminated,
        CastPlaybackState::Stopped,
        99,
        Some(CastTerminalReason::StoppedByReceiver),
    );
    e2e.facade.push_session_snapshot(stale_terminal);
    let stats = e2e.usecase.drain_session_events();
    assert_eq!(stats.dropped_stale, 1);
    assert_eq!(stats.terminal_converged, 0);
    assert_eq!(e2e.usecase.phase(), CastPhase::Casting);
    assert_eq!(e2e.usecase.active_session(), Some(new_session));
    assert_eq!(
        get_status(&casted_url(&e2e.facade)).await,
        200,
        "new relay session survives stale events"
    );
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn lifecycle_triggers_revoke_everything_idempotently() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    open_picker(&e2e);
    let _session = start_relay_cast(&e2e, &upstream, &e2e.device);
    let media_url = casted_url(&e2e.facade);

    e2e.usecase.on_navigation();
    assert_eq!(e2e.usecase.phase(), CastPhase::Browsing);
    assert_eq!(e2e.usecase.active_session(), None);
    assert_eq!(
        get_status(&media_url).await,
        401,
        "navigation revoked the token"
    );
    // Idempotent repetition and the remaining lifecycle triggers.
    e2e.usecase.on_navigation();
    e2e.usecase.on_profile_destroyed();
    assert_eq!(e2e.usecase.phase(), CastPhase::Idle);
    e2e.usecase.on_app_exit();
    assert_eq!(e2e.usecase.phase(), CastPhase::Idle);

    // Recovery: a new page cycle can cast again.
    open_picker(&e2e);
    let session = start_relay_cast(&e2e, &upstream, &e2e.device);
    assert_eq!(e2e.usecase.active_session(), Some(session));
    e2e.runtime.stop().await;
}

#[tokio::test]
async fn e2e_fake_direct_chain_forwards_url_verbatim() {
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: MOVIE_BODY.to_vec(),
        },
    )])
    .await
    .unwrap();
    let e2e = e2e().await;
    open_picker(&e2e);
    let outcome = e2e.usecase.start_cast(&request(
        &e2e.device,
        &upstream.url("/movie.mp4"),
        HeadersClass::None,
    ));
    let CastStartOutcome::Casting(session) = outcome else {
        panic!("expected casting: {outcome:?}")
    };
    // PL-002/CS-005: the candidate URL reaches the facade byte-for-byte and
    // no relay session exists for a Direct cast.
    assert_eq!(casted_url(&e2e.facade), upstream.url("/movie.mp4"));
    assert_eq!(
        e2e.runtime
            .trigger(RevokeReason::Stopped, Some(&e2e.device)),
        0
    );

    // Receiver stop converges and fences the old handle.
    e2e.usecase.drain_session_events();
    e2e.facade.simulate_receiver_stop();
    let stats = e2e.usecase.drain_session_events();
    assert_eq!(stats.terminal_converged, 1);
    assert_eq!(e2e.usecase.phase(), CastPhase::SelectingReceiver);
    assert_eq!(e2e.facade.pause(&session), Err(CastError::NoActiveSession));
    e2e.runtime.stop().await;
}
