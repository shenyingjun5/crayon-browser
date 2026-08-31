use super::cast_usecase::RelayRevocation;
use super::delivery::{DeliveryRequest, SessionBackend};
use super::media_host_cast_runtime::MediaHostCastRuntime;
use super::media_host_runtime::{MediaHostRuntime, MediaHostRuntimeError};
use crayon_cast_adapter::{
    AssessmentStatus, CastCode, CastError, CastFacade, CastMediaKind, CastPlaybackState,
    CastSessionPhase, CastSessionSnapshot, DeviceState, DiscoveredDevice, ReceiverCapabilityCache,
    SenderCastFacade, SenderCastFacadeConfig,
};
use crayon_cast_policy::HandoffAvailability;
use crayon_domain::{CoreError, DeviceId, ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyInput, HandoffReason, HeadersClass, MediaCandidate,
    MediaHostCastControlAction, MediaHostCastControlOutcome, MediaHostCastErrorCode,
    MediaHostCastStartOutcome, MediaHostDeliveryRoute, MediaHostDiscoveryAction, MediaHostMessage,
    MediaHostPlayback, MediaHostResolveCastCodeOutcome, MediaHostSessionPhase, MediaHostSource,
    MediaHostTerminalReason, MediaHostUrlFact, PageContext, PlaybackState, ProtocolKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;
use crayon_media_probe::{http::ProbeHttpClient, http::ProbeHttpConfig, MediaInspector};
use crayon_relay::session::RevokeReason;
use std::sync::Arc;
use test_support::cast_facade::{FakeCall, FakeCastFacade};

#[derive(Default)]
struct RecordingBackend;

impl SessionBackend for RecordingBackend {
    fn open(
        &mut self,
        _receiver: &DeviceId,
        _receiver_ip: Option<std::net::IpAddr>,
        _candidate_url: &str,
        _protocol: ProtocolKind,
        _headers_class: HeadersClass,
        _page_url: &str,
    ) -> Result<String, CoreError> {
        Ok("http://127.0.0.1:20001/s/fixture/master.m3u8".to_owned())
    }
}

#[derive(Default)]
struct RecordingRevocation;

impl RelayRevocation for RecordingRevocation {
    fn revoke(&self, _reason: RevokeReason, _receiver: Option<&DeviceId>) -> usize {
        0
    }
}

struct Harness {
    runtime: Arc<MediaHostCastRuntime>,
    facade: Arc<FakeCastFacade>,
    device: DeviceId,
}

fn harness() -> Harness {
    let facade = Arc::new(FakeCastFacade::new());
    let device = DeviceId::new("receiver-1").unwrap();
    facade.upsert_device(DiscoveredDevice::new(
        device.clone(),
        "Living Room".to_owned(),
        DeviceState::Ready,
        true,
    ));
    facade.set_assessment(&device, CastMediaKind::Video, AssessmentStatus::Supported);
    facade.set_assessment(&device, CastMediaKind::Hls, AssessmentStatus::Supported);
    let facade_port: Arc<dyn CastFacade> = facade.clone();
    let capabilities = Arc::new(ReceiverCapabilityCache::new(
        Arc::clone(&facade_port),
        Default::default(),
    ));
    let runtime = Arc::new(MediaHostCastRuntime::new(
        facade_port,
        capabilities,
        Box::new(RecordingBackend),
        Arc::new(RecordingRevocation),
    ));
    Harness {
        runtime,
        facade,
        device,
    }
}

#[test]
fn host_claims_start_and_fences_candidate_device_and_navigation() {
    let h = harness();
    initial_page(&h.runtime);
    let inspector = MediaInspector::new(ProbeHttpClient::new(ProbeHttpConfig::default()));
    let mut host = MediaHostRuntime::with_cast(inspector, Arc::clone(&h.runtime));
    let candidate = match host
        .handle_immediate(MediaHostMessage::IngestUrl(MediaHostUrlFact {
            request_id: "ingest".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
            observed_at_ms: 1,
            page_url: "https://page.example/watch".to_owned(),
            media_url: "https://media.example/movie.mp4".to_owned(),
            source: MediaHostSource::CurrentSrc,
            headers_class: HeadersClass::None,
            playback: Some(MediaHostPlayback {
                position_ms: 1_000,
                duration_ms: Some(10_000),
                is_live: false,
                ad_continuity: AdContinuity::Preserved,
                current_src: true,
                near_play_event: true,
                audible: true,
                main_frame: true,
                visible_area_px: 100,
            }),
            eme_encrypted: false,
        }))
        .unwrap()
        .unwrap()
    {
        MediaHostMessage::CandidateReply {
            candidate_id: Some(candidate),
            ..
        } => candidate,
        _ => panic!("expected candidate"),
    };
    assert!(host
        .prepare_cast_command(MediaHostMessage::StartCast {
            request_id: "start".to_owned(),
            candidate_id: candidate,
            device_id: h.device.as_str().to_owned(),
            handoff_available: true,
        })
        .is_ok());
    assert!(matches!(
        host.prepare_cast_command(MediaHostMessage::StartCast {
            request_id: "unknown-device".to_owned(),
            candidate_id: candidate,
            device_id: "unknown-device".to_owned(),
            handoff_available: true,
        }),
        Err(MediaHostRuntimeError::CandidateUnavailable)
    ));
    host.handle_immediate(MediaHostMessage::Navigation {
        request_id: "nav".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 2,
        generation: 2,
    })
    .unwrap();
    assert!(matches!(
        host.prepare_cast_command(MediaHostMessage::StartCast {
            request_id: "stale-candidate".to_owned(),
            candidate_id: candidate,
            device_id: h.device.as_str().to_owned(),
            handoff_available: true,
        }),
        Err(MediaHostRuntimeError::CandidateUnavailable)
    ));
}

#[tokio::test]
async fn real_facade_discovery_list_stop_and_shutdown_contract() {
    let facade = Arc::new(SenderCastFacade::new(SenderCastFacadeConfig::default()));
    let facade_port: Arc<dyn CastFacade> = facade.clone();
    let capabilities = Arc::new(ReceiverCapabilityCache::new(
        Arc::clone(&facade_port),
        Default::default(),
    ));
    let runtime = MediaHostCastRuntime::new(
        facade_port,
        capabilities,
        Box::new(RecordingBackend),
        Arc::new(RecordingRevocation),
    );
    assert!(runtime
        .discovery("real-start".to_owned(), MediaHostDiscoveryAction::Start)
        .is_ok());
    assert!(matches!(
        runtime
            .list_devices("real-list".to_owned(), None, 0)
            .unwrap(),
        MediaHostMessage::DevicePageReply { devices, .. } if devices.len() <= 16
    ));
    assert!(runtime.stop_cast("real-stop".to_owned(), 1).await.is_ok());
    assert!(runtime
        .discovery(
            "real-stop-discovery".to_owned(),
            MediaHostDiscoveryAction::Stop
        )
        .is_ok());
    runtime.on_app_exit();
    facade.shutdown();
}

fn request(device: &DeviceId, protection: Protection) -> DeliveryRequest {
    request_for(device, protection, ProtocolKind::Mp4, HeadersClass::None)
}

fn request_for(
    device: &DeviceId,
    protection: Protection,
    protocol: ProtocolKind,
    headers_class: HeadersClass,
) -> DeliveryRequest {
    DeliveryRequest {
        input: CastPolicyInput::new(
            PageContext::new(
                TabId::new("tab-1").unwrap(),
                "https://page.example/watch".to_owned(),
            ),
            PlaybackState::new(10.0, Some(60.0), false),
            MediaCandidate::new(
                "https://media.example/movie.mp4".to_owned(),
                protocol,
                protection == Protection::DrmProtected,
                headers_class,
                None,
                None,
                AdContinuity::Preserved,
            ),
            ReceiverCapabilities::new(false, false, false, false, false, false, 0),
        ),
        observation: PlaybackObservation::new(
            ObservationOrigin::BrowserVerified,
            UserActivation::BrowserVerified,
            PlaybackProgress::Advanced,
        ),
        protection,
        external_client_handoff: HandoffAvailability::Available,
        receiver: device.clone(),
        receiver_ip: None,
    }
}

fn initial_page(runtime: &MediaHostCastRuntime) -> (u64, Vec<String>, Option<u16>) {
    match runtime.list_devices("list-1".to_owned(), None, 0).unwrap() {
        MediaHostMessage::DevicePageReply {
            snapshot_revision,
            devices,
            next_offset,
            ..
        } => (
            snapshot_revision,
            devices.into_iter().map(|device| device.device_id).collect(),
            next_offset,
        ),
        _ => panic!("expected device page"),
    }
}

#[test]
fn discovery_pages_are_revision_bound_and_bounded() {
    let h = harness();
    for index in 2..=17 {
        h.facade.upsert_device(DiscoveredDevice::new(
            DeviceId::new(&format!("receiver-{index}")).unwrap(),
            format!("Room {index:02}"),
            DeviceState::Ready,
            false,
        ));
    }
    assert!(matches!(
        h.runtime
            .discovery("discover".to_owned(), MediaHostDiscoveryAction::Start)
            .unwrap(),
        MediaHostMessage::Ack { .. }
    ));
    let (revision, first, next) = initial_page(&h.runtime);
    assert_eq!(first.len(), 16);
    assert_eq!(next, Some(16));
    let last = h
        .runtime
        .list_devices("list-2".to_owned(), Some(revision), 16)
        .unwrap();
    assert!(matches!(
        last,
        MediaHostMessage::DevicePageReply {
            devices,
            next_offset: None,
            ..
        } if devices.len() == 1
    ));
    h.facade.remove_device(&h.device);
    let (new_revision, _, _) = initial_page(&h.runtime);
    assert!(new_revision > revision);
    assert!(matches!(
        h.runtime
            .list_devices("stale".to_owned(), Some(revision), 0),
        Err(MediaHostRuntimeError::StaleContext)
    ));
    assert!(matches!(
        h.runtime
            .discovery("refresh".to_owned(), MediaHostDiscoveryAction::Refresh)
            .unwrap(),
        MediaHostMessage::Ack { .. }
    ));
    assert!(matches!(
        h.runtime
            .discovery("stop".to_owned(), MediaHostDiscoveryAction::Stop)
            .unwrap(),
        MediaHostMessage::Ack { .. }
    ));
}

#[test]
fn device_snapshot_rejects_invalid_names_and_capacity_but_allows_same_names() {
    let h = harness();
    h.facade.upsert_device(DiscoveredDevice::new(
        DeviceId::new("receiver-2").unwrap(),
        "Living Room".to_owned(),
        DeviceState::Ready,
        false,
    ));
    assert!(h
        .runtime
        .list_devices("same-name".to_owned(), None, 0)
        .is_ok());
    h.facade.upsert_device(DiscoveredDevice::new(
        DeviceId::new("receiver-empty").unwrap(),
        String::new(),
        DeviceState::Ready,
        false,
    ));
    assert!(matches!(
        h.runtime.list_devices("empty-name".to_owned(), None, 0),
        Err(MediaHostRuntimeError::InvalidMessage)
    ));

    let h = harness();
    for index in 2..=65 {
        h.facade.upsert_device(DiscoveredDevice::new(
            DeviceId::new(&format!("receiver-{index}")).unwrap(),
            format!("Room {index}"),
            DeviceState::Ready,
            false,
        ));
    }
    assert!(matches!(
        h.runtime.list_devices("capacity".to_owned(), None, 0),
        Err(MediaHostRuntimeError::CapacityExceeded)
    ));
}

#[tokio::test]
async fn direct_start_stop_and_terminal_events_are_wire_fenced() {
    let h = harness();
    initial_page(&h.runtime);
    let reply = h
        .runtime
        .start_cast("cast-1".to_owned(), request(&h.device, Protection::Clear))
        .await
        .unwrap();
    assert!(matches!(
        reply,
        MediaHostMessage::StartCastReply {
            outcome: MediaHostCastStartOutcome::Casting {
                session_generation: 1,
                route: MediaHostDeliveryRoute::Direct,
            },
            ..
        }
    ));
    assert!(matches!(
        h.runtime.stop_cast("stale".to_owned(), 2).await,
        Err(MediaHostRuntimeError::StaleContext)
    ));
    assert!(matches!(
        h.runtime.stop_cast("stop".to_owned(), 1).await.unwrap(),
        MediaHostMessage::Ack { .. }
    ));
    assert!(matches!(
        h.runtime
            .stop_cast("stop-again".to_owned(), 1)
            .await
            .unwrap(),
        MediaHostMessage::Ack { .. }
    ));
    let events = h.runtime.poll_session_events("events".to_owned()).unwrap();
    assert!(matches!(
        events,
        MediaHostMessage::SessionEventsReply { events, .. }
            if events.len() == 3
                && events.iter().all(|event| event.session_generation == 1)
                && events.iter().all(|event| event.state_revision > 0)
                && events.last().is_some_and(|event|
                    event.phase == MediaHostSessionPhase::Terminated
                    && event.terminal_reason == Some(MediaHostTerminalReason::StoppedBySender))
    ));
    assert!(matches!(
        h.runtime
            .stop_cast("after-terminal".to_owned(), 1)
            .await
            .unwrap(),
        MediaHostMessage::Ack { .. }
    ));
}

#[tokio::test]
async fn cast_code_and_controls_use_facade_and_wire_generation_fencing() {
    let h = harness();
    assert!(matches!(
        h.runtime
            .control_cast(
                "no-session".to_owned(),
                1,
                MediaHostCastControlAction::Pause,
                None,
            )
            .await
            .unwrap(),
        MediaHostMessage::ControlCastReply {
            outcome: MediaHostCastControlOutcome::Failed(MediaHostCastErrorCode::NoActiveSession),
            ..
        }
    ));
    let code = CastCode::new("AB1-CD2").unwrap();
    h.facade.bind_cast_code(&code, &h.device);
    let resolved = h
        .runtime
        .resolve_cast_code("resolve".to_owned(), "AB1 CD2".to_owned())
        .unwrap();
    assert!(matches!(
        resolved,
        MediaHostMessage::ResolveCastCodeReply {
            outcome: MediaHostResolveCastCodeOutcome::Resolved(device),
            ..
        }
            if device.device_id == h.device.as_str()
                && device.is_crayon_receiver
    ));
    assert!(matches!(
        h.runtime
            .resolve_cast_code("bad".to_owned(), "bad".to_owned())
            .unwrap(),
        MediaHostMessage::ResolveCastCodeReply {
            outcome: MediaHostResolveCastCodeOutcome::Failed(
                MediaHostCastErrorCode::InvalidCastCode
            ),
            ..
        }
    ));
    assert!(matches!(
        h.runtime
            .resolve_cast_code("missing".to_owned(), "ZZZ999".to_owned())
            .unwrap(),
        MediaHostMessage::ResolveCastCodeReply {
            outcome: MediaHostResolveCastCodeOutcome::Failed(
                MediaHostCastErrorCode::DeviceNotFound
            ),
            ..
        }
    ));

    h.runtime
        .start_cast("cast".to_owned(), request(&h.device, Protection::Clear))
        .await
        .unwrap();
    assert!(matches!(
        h.runtime
            .control_cast(
                "stale".to_owned(),
                2,
                MediaHostCastControlAction::Pause,
                None,
            )
            .await,
        Ok(MediaHostMessage::ControlCastReply {
            outcome: MediaHostCastControlOutcome::Failed(
                MediaHostCastErrorCode::StaleSessionGeneration
            ),
            ..
        })
    ));
    for (request_id, action, position_seconds) in [
        ("pause", MediaHostCastControlAction::Pause, None),
        ("play", MediaHostCastControlAction::Play, None),
        ("seek", MediaHostCastControlAction::Seek, Some(30)),
    ] {
        assert!(matches!(
            h.runtime
                .control_cast(request_id.to_owned(), 1, action, position_seconds)
                .await
                .unwrap(),
            MediaHostMessage::ControlCastReply {
                outcome: MediaHostCastControlOutcome::Applied,
                ..
            }
        ));
    }
    let calls = h.facade.calls();
    assert!(calls
        .iter()
        .any(|call| matches!(call, FakeCall::ResolveCastCode(value) if value == "AB1CD2")));
    assert!(calls.iter().any(|call| matches!(call, FakeCall::Pause(_))));
    assert!(calls.iter().any(|call| matches!(call, FakeCall::Play(_))));
    assert!(calls.iter().any(|call| matches!(
        call,
        FakeCall::Seek {
            position_seconds: 30,
            ..
        }
    )));
}

#[tokio::test]
async fn relay_start_reports_only_the_closed_route() {
    let h = harness();
    initial_page(&h.runtime);
    let reply = h
        .runtime
        .start_cast(
            "relay".to_owned(),
            request_for(
                &h.device,
                Protection::Clear,
                ProtocolKind::Hls,
                HeadersClass::RefererAndUa,
            ),
        )
        .await
        .unwrap();
    assert!(matches!(
        reply,
        MediaHostMessage::StartCastReply {
            outcome: MediaHostCastStartOutcome::Casting {
                session_generation: 1,
                route: MediaHostDeliveryRoute::Relay,
            },
            ..
        }
    ));
}

#[tokio::test]
async fn handoff_reject_and_facade_failure_are_closed_outcomes() {
    let h = harness();
    initial_page(&h.runtime);
    let handoff = h
        .runtime
        .start_cast(
            "handoff".to_owned(),
            request(&h.device, Protection::NoDirectUrl),
        )
        .await
        .unwrap();
    assert!(matches!(
        handoff,
        MediaHostMessage::StartCastReply {
            outcome: MediaHostCastStartOutcome::Handoff {
                reason: HandoffReason::NoDirectUrl
            },
            ..
        }
    ));
    let rejected = h
        .runtime
        .start_cast(
            "reject".to_owned(),
            request(&h.device, Protection::DrmProtected),
        )
        .await
        .unwrap();
    assert!(matches!(
        rejected,
        MediaHostMessage::StartCastReply {
            outcome: MediaHostCastStartOutcome::Rejected {
                reason: CoreError::DrmProtected
            },
            ..
        }
    ));
    h.facade.fail_next_connect(CastError::RouteLost);
    let failed = h
        .runtime
        .start_cast("failed".to_owned(), request(&h.device, Protection::Clear))
        .await
        .unwrap();
    assert!(matches!(
        failed,
        MediaHostMessage::StartCastReply {
            outcome: MediaHostCastStartOutcome::Failed {
                code: MediaHostCastErrorCode::RouteLost
            },
            ..
        }
    ));
}

#[tokio::test]
async fn event_pump_caps_batches_and_counts_overflow_and_stale() {
    let h = harness();
    initial_page(&h.runtime);
    h.runtime
        .start_cast("cast".to_owned(), request(&h.device, Protection::Clear))
        .await
        .unwrap();
    h.runtime.poll_session_events("initial".to_owned()).unwrap();
    for _ in 0..140 {
        h.facade
            .drive_session(CastSessionPhase::Active, CastPlaybackState::Playing, None);
    }
    let first = h.runtime.poll_session_events("batch-1".to_owned()).unwrap();
    let dropped = match first {
        MediaHostMessage::SessionEventsReply {
            dropped_events,
            events,
            ..
        } => {
            assert_eq!(events.len(), 64);
            assert!(dropped_events >= 12);
            dropped_events
        }
        _ => panic!("expected event batch"),
    };
    let second = h.runtime.poll_session_events("batch-2".to_owned()).unwrap();
    assert!(matches!(
        second,
        MediaHostMessage::SessionEventsReply {
            dropped_events,
            events,
            ..
        } if dropped_events == dropped && events.len() == 64
    ));
    let current = h.facade.current_session().unwrap();
    h.facade.push_session_snapshot(CastSessionSnapshot::new(
        current.session().clone(),
        CastSessionPhase::Active,
        CastPlaybackState::Playing,
        1,
        None,
    ));
    let stale = h.runtime.poll_session_events("stale".to_owned()).unwrap();
    assert!(matches!(
        stale,
        MediaHostMessage::SessionEventsReply {
            dropped_events,
            events,
            ..
        } if dropped_events == dropped + 1 && events.is_empty()
    ));
}
