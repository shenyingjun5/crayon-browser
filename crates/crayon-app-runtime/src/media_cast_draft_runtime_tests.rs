use super::cast_usecase::RelayRevocation;
use super::delivery::SessionBackend;
use super::media_cast_draft_runtime::{wire_preflight, MediaCastDraftRuntime};
use super::media_host_cast_runtime::MediaHostCastRuntime;
use super::media_host_runtime::MediaHostRuntime;
use super::media_planning_runtime::LocalPreflightStatus;
use super::media_player_registry::MediaPlayerRegistry;
use crayon_cast_adapter::{
    AssessmentStatus, CastFacade, CastMediaKind, DeviceState, DiscoveredDevice,
    ReceiverCapabilityCache,
};
use crayon_domain::{CoreError, DeviceId};
use crayon_ipc_schema::media_host_v2::{
    DraftAction, DraftCommand, DraftContext, DraftMediaRef, DraftPhase, DraftReason, DraftRoute,
    PlayerContext, PlayerFact, PlayerMessage, PlayerSourceKind,
};
use crayon_ipc_schema::{
    AdContinuity, HeadersClass, MediaHostMessage, MediaHostPlayback, MediaHostSource,
    MediaHostUrlFact, ProtocolKind,
};
use crayon_media_probe::http::{ProbeHttpClient, ProbeHttpConfig};
use crayon_media_probe::{InspectionStatus, MediaInspector, ProbeHttpError};
use crayon_relay::session::RevokeReason;
use std::sync::Arc;
use test_support::cast_facade::{FakeCall, FakeCastFacade};

#[derive(Default)]
struct Backend;

impl SessionBackend for Backend {
    fn open(
        &mut self,
        _receiver: &DeviceId,
        _receiver_ip: Option<std::net::IpAddr>,
        _candidate_url: &str,
        _protocol: ProtocolKind,
        _headers_class: HeadersClass,
        _page_url: &str,
    ) -> Result<String, CoreError> {
        Ok("http://127.0.0.1:20001/s/test/media".into())
    }
}

#[derive(Default)]
struct Revocation;

impl RelayRevocation for Revocation {
    fn revoke(&self, _reason: RevokeReason, _receiver: Option<&DeviceId>) -> usize {
        0
    }
}

#[test]
fn preflight_reasons_are_closed_and_exhaustively_projected() {
    let cases = [
        (
            LocalPreflightStatus::SkippedCredentials,
            DraftReason::Credentials,
        ),
        (
            LocalPreflightStatus::SkippedProtection,
            DraftReason::Protection,
        ),
        (
            LocalPreflightStatus::Inspected(InspectionStatus::Recognized),
            DraftReason::Recognized,
        ),
        (
            LocalPreflightStatus::Inspected(InspectionStatus::Unrecognized),
            DraftReason::Unrecognized,
        ),
        (
            LocalPreflightStatus::Inspected(InspectionStatus::RedirectRefused),
            DraftReason::RedirectRefused,
        ),
        (
            LocalPreflightStatus::Inspected(InspectionStatus::UpstreamRejected),
            DraftReason::UpstreamRejected,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::NonPublicAddress),
            DraftReason::AddressRejected,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::Dns),
            DraftReason::Dns,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::Connect),
            DraftReason::Connect,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::Timeout),
            DraftReason::Timeout,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::Transport),
            DraftReason::Transport,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::UnsupportedScheme),
            DraftReason::InvalidTarget,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::InvalidUrl),
            DraftReason::InvalidTarget,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::InvalidRange),
            DraftReason::InvalidTarget,
        ),
        (
            LocalPreflightStatus::Failed(ProbeHttpError::ScopeMismatch),
            DraftReason::InvalidTarget,
        ),
    ];
    for (input, expected) in cases {
        assert_eq!(wire_preflight(&input), expected);
    }
}

fn command(request_id: u64, action: DraftAction, draft_id: u64, revision: u64) -> DraftCommand {
    DraftCommand {
        context: DraftContext {
            session_id: 7,
            host_generation: 9,
            request_id,
            profile_id: "default".into(),
            tab_id: 1,
            navigation_id: 2,
            tab_generation: 3,
        },
        action,
        draft_id,
        draft_revision: revision,
        media: None,
        device_id: String::new(),
    }
}

#[tokio::test]
async fn connect_prepare_and_commit_are_distinct_and_single_use() {
    use test_support::upstream::{MockUpstream, UpstreamScript};

    let upstream = MockUpstream::start(vec![(
        "/video.mp4".into(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".into()),
            body: b"\x00\x00\x00\x18ftypmp42........".to_vec(),
        },
    )])
    .await
    .unwrap();
    let media_url = upstream.url("/video.mp4");
    let facade = Arc::new(FakeCastFacade::new());
    let device = DeviceId::new("receiver-1").unwrap();
    facade.upsert_device(DiscoveredDevice::new(
        device.clone(),
        "Living Room".into(),
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
    let cast = Arc::new(MediaHostCastRuntime::new(
        facade_port,
        capabilities,
        Box::new(Backend),
        Arc::new(Revocation),
    ));
    cast.list_devices("sync".into(), None, 0).unwrap();
    let mut host = MediaHostRuntime::with_cast(
        MediaInspector::new(ProbeHttpClient::new(ProbeHttpConfig {
            allow_private_addresses: true,
            ..ProbeHttpConfig::default()
        })),
        cast,
    );
    host.handle_immediate(MediaHostMessage::IngestUrl(MediaHostUrlFact {
        request_id: "ingest".into(),
        tab_id: "cef-1".into(),
        navigation_id: 2,
        generation: 3,
        observed_at_ms: 1,
        page_url: media_url.clone(),
        media_url: media_url.clone(),
        source: MediaHostSource::CurrentSrc,
        headers_class: HeadersClass::None,
        playback: Some(MediaHostPlayback {
            position_ms: 1_000,
            duration_ms: Some(60_000),
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
    .unwrap();
    let media = DraftMediaRef {
        instance_id: 11,
        source_revision: 1,
    };
    let mut players = MediaPlayerRegistry::new(7, 9).unwrap();
    players
        .apply(PlayerMessage::Upsert(PlayerFact {
            context: PlayerContext {
                session_id: 7,
                host_generation: 9,
                tab_id: 1,
                navigation_id: 2,
                tab_generation: 3,
                instance_id: media.instance_id,
                source_revision: media.source_revision,
            },
            observed_at_ms: 1,
            source_kind: PlayerSourceKind::HttpUrl,
            position_ms: 1_000,
            duration_ms: Some(60_000),
            is_live: false,
            has_video: true,
            has_audio: true,
            visible: true,
            eme_encrypted: false,
            visible_fraction_ppm: 1_000_000,
            page_url: media_url.clone(),
            media_url,
        }))
        .unwrap();
    let mut runtime = MediaCastDraftRuntime::new();
    let opened = runtime
        .dispatch(command(1, DraftAction::Open, 0, 0), &players, &mut host, 10)
        .await;
    let mut select_device = command(
        2,
        DraftAction::SelectDevice,
        opened.draft_id,
        opened.draft_revision,
    );
    select_device.device_id = device.as_str().into();
    let selected = runtime
        .dispatch(select_device, &players, &mut host, 11)
        .await;
    let connected = runtime
        .dispatch(
            command(
                3,
                DraftAction::Connect,
                selected.draft_id,
                selected.draft_revision,
            ),
            &players,
            &mut host,
            12,
        )
        .await;
    assert!(connected.device_connected);
    assert!(!facade
        .calls()
        .iter()
        .any(|call| matches!(call, FakeCall::CastMedia { .. })));

    let mut select_media = command(
        4,
        DraftAction::SelectMedia,
        connected.draft_id,
        connected.draft_revision,
    );
    select_media.media = Some(media);
    let selected = runtime
        .dispatch(select_media, &players, &mut host, 13)
        .await;
    let prepared = runtime
        .dispatch(
            command(
                5,
                DraftAction::Prepare,
                selected.draft_id,
                selected.draft_revision,
            ),
            &players,
            &mut host,
            14,
        )
        .await;
    assert_eq!(prepared.phase, DraftPhase::Prepared);
    assert_eq!(prepared.route, DraftRoute::Direct);
    assert_eq!(prepared.reason, DraftReason::Recognized);
    assert_eq!(prepared.prepared_until_ms, Some(15_014));
    assert!(!facade
        .calls()
        .iter()
        .any(|call| matches!(call, FakeCall::CastMedia { .. })));

    let reopened = runtime
        .dispatch(command(6, DraftAction::Open, 0, 0), &players, &mut host, 15)
        .await;
    let replaced = runtime
        .dispatch(
            command(
                7,
                DraftAction::Commit,
                prepared.draft_id,
                prepared.draft_revision,
            ),
            &players,
            &mut host,
            15,
        )
        .await;
    assert_eq!(replaced.phase, DraftPhase::Failed);
    assert!(!facade
        .calls()
        .iter()
        .any(|call| matches!(call, FakeCall::CastMedia { .. })));

    let mut select_device = command(
        8,
        DraftAction::SelectDevice,
        reopened.draft_id,
        reopened.draft_revision,
    );
    select_device.device_id = device.as_str().into();
    let selected = runtime
        .dispatch(select_device, &players, &mut host, 16)
        .await;
    let connected = runtime
        .dispatch(
            command(
                9,
                DraftAction::Connect,
                selected.draft_id,
                selected.draft_revision,
            ),
            &players,
            &mut host,
            17,
        )
        .await;
    let mut select_media = command(
        10,
        DraftAction::SelectMedia,
        connected.draft_id,
        connected.draft_revision,
    );
    select_media.media = Some(media);
    let selected = runtime
        .dispatch(select_media, &players, &mut host, 18)
        .await;
    let prepared = runtime
        .dispatch(
            command(
                11,
                DraftAction::Prepare,
                selected.draft_id,
                selected.draft_revision,
            ),
            &players,
            &mut host,
            19,
        )
        .await;
    assert_eq!(prepared.phase, DraftPhase::Prepared);

    let stale = runtime
        .dispatch(
            command(
                12,
                DraftAction::Commit,
                prepared.draft_id,
                prepared.draft_revision - 1,
            ),
            &players,
            &mut host,
            20,
        )
        .await;
    assert_eq!(stale.phase, DraftPhase::Failed);
    assert!(!facade
        .calls()
        .iter()
        .any(|call| matches!(call, FakeCall::CastMedia { .. })));

    let committed = runtime
        .dispatch(
            command(
                13,
                DraftAction::Commit,
                prepared.draft_id,
                prepared.draft_revision,
            ),
            &players,
            &mut host,
            20,
        )
        .await;
    assert_eq!(committed.phase, DraftPhase::Committed);
    assert!(committed.session_generation.is_some());
    assert_eq!(
        facade
            .calls()
            .iter()
            .filter(|call| matches!(call, FakeCall::CastMedia { .. }))
            .count(),
        1
    );
    let repeated = runtime
        .dispatch(
            command(
                14,
                DraftAction::Commit,
                prepared.draft_id,
                prepared.draft_revision,
            ),
            &players,
            &mut host,
            21,
        )
        .await;
    assert_eq!(repeated.phase, DraftPhase::Failed);
    assert_eq!(
        facade
            .calls()
            .iter()
            .filter(|call| matches!(call, FakeCall::CastMedia { .. }))
            .count(),
        1
    );
}
