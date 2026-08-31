use super::media_planning_runtime::{
    MediaPlanningError, MediaPlanningRuntime, VerifiedPlayback, VerifiedUrlFact,
};
use crayon_cast_policy::HandoffAvailability;
use crayon_domain::{CoreError, ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, ExternalClientHandoff, HandoffReason, HeadersClass,
    PlaybackState, ProtocolKind,
};
use crayon_media_observer::candidate::{LifecyclePolicy, RankingSignals};
use crayon_media_observer::ObservationSource;
use crayon_media_probe::http::{ProbeHttpClient, ProbeHttpConfig};
use crayon_media_probe::MediaInspector;
use std::time::Duration;
use test_support::upstream::{drip, MockUpstream, UpstreamScript};

fn runtime() -> MediaPlanningRuntime {
    runtime_with_config(ProbeHttpConfig {
        allow_private_addresses: true,
        ..ProbeHttpConfig::default()
    })
}

fn runtime_with_config(config: ProbeHttpConfig) -> MediaPlanningRuntime {
    MediaPlanningRuntime::new(MediaInspector::new(ProbeHttpClient::new(config)))
}

fn all_capabilities() -> ReceiverCapabilities {
    ReceiverCapabilities::new(true, true, true, true, true, true, 2160)
}

fn playback(position: f64) -> VerifiedPlayback {
    VerifiedPlayback {
        state: PlaybackState::new(position, Some(300.0), false),
        ad_continuity: AdContinuity::Preserved,
        ranking: RankingSignals::new(true, true, true, true, 100),
    }
}

fn fact(
    media_url: String,
    source: ObservationSource,
    headers_class: HeadersClass,
    verified_playback: Option<VerifiedPlayback>,
) -> VerifiedUrlFact {
    VerifiedUrlFact {
        tab_id: TabId::new("tab-1").unwrap(),
        navigation_id: 1,
        page_url: "https://page.example/watch".to_owned(),
        media_url,
        source,
        observed_at_ms: 100,
        headers_class,
        playback: verified_playback,
        eme_encrypted: false,
    }
}

fn full(content_type: &str, body: Vec<u8>) -> UpstreamScript {
    UpstreamScript::Full {
        status: 200,
        content_type: Some(content_type.to_owned()),
        body,
    }
}

fn mp4() -> Vec<u8> {
    let mut bytes = vec![0, 0, 0, 24];
    bytes.extend_from_slice(b"ftypmp42");
    bytes.extend_from_slice(&[0; 32]);
    bytes
}

#[tokio::test]
async fn clear_mp4_is_direct_without_exposing_url() {
    let upstream = MockUpstream::start(vec![(
        "/clear.mp4?signature=fixture-value".to_owned(),
        UpstreamScript::HeadRejected(Box::new(UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_owned()),
            body: mp4(),
        })),
    )])
    .await
    .unwrap();
    let mut owner = runtime();
    let summary = owner
        .ingest_url(fact(
            upstream.url("/clear.mp4?signature=fixture-value"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    assert_eq!(summary.redacted_origin, upstream.base_url());
    assert!(!format!("{summary:?}").contains("signature"));

    let plan = owner
        .decide_for_receiver(
            summary.id,
            all_capabilities(),
            HandoffAvailability::Available,
        )
        .await
        .unwrap();
    assert_eq!(plan.protocol, ProtocolKind::Mp4);
    assert_eq!(plan.decision, CastPolicyDecision::Direct);
}

#[tokio::test]
async fn hls_header_evidence_routes_only_through_relay() {
    let playlist =
        b"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,CODECS=\"avc1.64001f,mp4a.40.2\"\nv.m3u8\n";
    let upstream = MockUpstream::start(vec![(
        "/master.m3u8".to_owned(),
        full("application/vnd.apple.mpegurl", playlist.to_vec()),
    )])
    .await
    .unwrap();
    let url = upstream.url("/master.m3u8");
    let mut owner = runtime();
    let summary = owner
        .ingest_url(fact(
            url.clone(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    assert!(owner
        .ingest_url(fact(
            url,
            ObservationSource::NetworkRequest,
            HeadersClass::RefererAndUa,
            None,
        ))
        .unwrap()
        .is_some());
    let plan = owner
        .decide_for_receiver(
            summary.id,
            all_capabilities(),
            HandoffAvailability::Available,
        )
        .await
        .unwrap();
    assert_eq!(plan.protocol, ProtocolKind::Hls);
    assert_eq!(plan.decision, CastPolicyDecision::Relay);
}

#[tokio::test]
async fn encrypted_hls_never_fetches_key_or_selects_a_cast_route() {
    let playlist = b"#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n#EXTINF:4,\nseg.ts\n";
    let upstream = MockUpstream::start(vec![
        (
            "/encrypted.m3u8".to_owned(),
            full("application/vnd.apple.mpegurl", playlist.to_vec()),
        ),
        (
            "/key.bin".to_owned(),
            full("application/octet-stream", vec![7; 16]),
        ),
    ])
    .await
    .unwrap();
    let mut owner = runtime();
    let candidate = owner
        .ingest_url(fact(
            upstream.url("/encrypted.m3u8"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    let decision = owner
        .decide_for_receiver(
            candidate.id,
            all_capabilities(),
            HandoffAvailability::Available,
        )
        .await
        .unwrap();
    assert_eq!(
        decision.decision,
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::KeyRequired
        ))
    );
    assert_eq!(upstream.hit_count("/key.bin"), 0);
}

#[tokio::test]
async fn dash_content_protection_and_eme_are_stable_rejections() {
    let upstream = MockUpstream::start(vec![(
        "/manifest.mpd".to_owned(),
        full(
            "application/dash+xml",
            b"<MPD><ContentProtection/><Representation/></MPD>".to_vec(),
        ),
    )])
    .await
    .unwrap();
    let mut owner = runtime();
    let dash = owner
        .ingest_url(fact(
            upstream.url("/manifest.mpd"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    let decision = owner
        .decide_for_receiver(dash.id, all_capabilities(), HandoffAvailability::Available)
        .await
        .unwrap();
    assert_eq!(decision.protocol, ProtocolKind::Dash);
    assert_eq!(
        decision.decision,
        CastPolicyDecision::Reject {
            reason: CoreError::DrmProtected
        }
    );

    let mut eme_fact = fact(
        upstream.url("/clear.mp4"),
        ObservationSource::CurrentSrc,
        HeadersClass::None,
        Some(playback(120.0)),
    );
    eme_fact.eme_encrypted = true;
    let eme = owner.ingest_url(eme_fact).unwrap().unwrap();
    let decision = owner
        .decide_for_receiver(eme.id, all_capabilities(), HandoffAvailability::Available)
        .await
        .unwrap();
    assert_eq!(
        decision.decision,
        CastPolicyDecision::Reject {
            reason: CoreError::DrmProtected
        }
    );
    assert_eq!(upstream.hit_count("/clear.mp4"), 0, "EME skips probe");
}

#[tokio::test]
async fn credential_bound_skips_probe_and_never_directs() {
    let upstream = MockUpstream::start(vec![("/private.mp4".to_owned(), full("video/mp4", mp4()))])
        .await
        .unwrap();
    let url = upstream.url("/private.mp4");
    let mut owner = runtime();
    let candidate = owner
        .ingest_url(fact(
            url.clone(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    owner
        .ingest_url(fact(
            url,
            ObservationSource::NetworkRequest,
            HeadersClass::CredentialBound,
            None,
        ))
        .unwrap();
    let decision = owner
        .decide_for_receiver(
            candidate.id,
            all_capabilities(),
            HandoffAvailability::Available,
        )
        .await
        .unwrap();
    assert_eq!(
        decision.decision,
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::CredentialBound
        ))
    );
    assert_eq!(upstream.hit_count("/private.mp4"), 0);
}

#[test]
fn url_less_sources_never_receive_a_fabricated_url() {
    let state = PlaybackState::new(10.0, None, true);
    let tab = TabId::new("tab-1").unwrap();
    let page_url = "https://page.example/watch".to_owned();
    assert_eq!(
        MediaPlanningRuntime::decide_url_less(
            tab.clone(),
            page_url.clone(),
            state,
            false,
            HandoffAvailability::Available,
        ),
        Ok(CastPolicyDecision::ExternalClientHandoff(
            ExternalClientHandoff::new(HandoffReason::NoDirectUrl)
        ))
    );
    assert_eq!(
        MediaPlanningRuntime::decide_url_less(
            tab,
            page_url,
            state,
            true,
            HandoffAvailability::Available,
        ),
        Ok(CastPolicyDecision::Reject {
            reason: CoreError::DrmProtected
        })
    );
}

#[test]
fn mismatched_fact_fields_and_invalid_time_are_rejected() {
    let mut owner = runtime();
    let mut network_playback = fact(
        "https://media.example/a.mp4".to_owned(),
        ObservationSource::NetworkRequest,
        HeadersClass::None,
        Some(playback(1.0)),
    );
    assert_eq!(
        owner.ingest_url(network_playback),
        Err(MediaPlanningError::InvalidFact)
    );

    network_playback = fact(
        "https://media.example/a.mp4".to_owned(),
        ObservationSource::CurrentSrc,
        HeadersClass::CredentialBound,
        None,
    );
    assert_eq!(
        owner.ingest_url(network_playback),
        Err(MediaPlanningError::InvalidFact)
    );

    let invalid = VerifiedPlayback {
        state: PlaybackState::new(f64::NAN, Some(-1.0), false),
        ad_continuity: AdContinuity::Unknown,
        ranking: RankingSignals::default(),
    };
    assert_eq!(
        owner.ingest_url(fact(
            "https://media.example/a.mp4".to_owned(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(invalid),
        )),
        Err(MediaPlanningError::InvalidFact)
    );
    assert_eq!(
        MediaPlanningRuntime::decide_url_less(
            TabId::new("tab-1").unwrap(),
            "https://page.example/watch".to_owned(),
            PlaybackState::new(-1.0, None, false),
            false,
            HandoffAvailability::Available,
        ),
        Err(MediaPlanningError::InvalidFact)
    );
}

#[test]
fn mismatched_page_context_is_rejected_without_mutating_candidates() {
    let mut owner = runtime();
    let candidate = owner
        .ingest_url(fact(
            "https://media.example/a.mp4".to_owned(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(1.0)),
        ))
        .unwrap()
        .unwrap();
    let before = owner.candidates();
    let mut mismatched = fact(
        "https://media.example/b.mp4".to_owned(),
        ObservationSource::CurrentSrc,
        HeadersClass::None,
        Some(playback(2.0)),
    );
    mismatched.page_url = "https://other.example/watch".to_owned();

    assert_eq!(
        owner.ingest_url(mismatched),
        Err(MediaPlanningError::InvalidFact)
    );
    assert_eq!(owner.candidates(), before);
    assert_eq!(owner.retained_count(), 1);
    assert_eq!(owner.candidates()[0].id, candidate.id);
}

#[tokio::test]
async fn probe_failure_and_receiver_mismatch_fail_closed_without_retry() {
    let upstream = MockUpstream::start(Vec::new()).await.unwrap();
    let mut owner = runtime();
    let unknown = owner
        .ingest_url(fact(
            upstream.url("/missing.mp4"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    let decision = owner
        .decide_for_receiver(
            unknown.id,
            all_capabilities(),
            HandoffAvailability::Available,
        )
        .await
        .unwrap();
    assert_eq!(
        decision.decision,
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::ProbeInconclusive
        ))
    );
    assert_eq!(upstream.hit_count("/missing.mp4"), 1, "no retry");

    let clear_upstream = MockUpstream::start(vec![(
        "/clear.mp4".to_owned(),
        UpstreamScript::HeadRejected(Box::new(UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_owned()),
            body: mp4(),
        })),
    )])
    .await
    .unwrap();
    let clear = owner
        .ingest_url(fact(
            clear_upstream.url("/clear.mp4"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    let incompatible = ReceiverCapabilities::new(false, false, false, false, false, false, 0);
    let decision = owner
        .decide_for_receiver(clear.id, incompatible, HandoffAvailability::Available)
        .await
        .unwrap();
    assert_eq!(
        decision.decision,
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::ReceiverIncompatible
        ))
    );
}

#[tokio::test]
async fn stalled_probe_times_out_to_inconclusive_without_retry() {
    let (stalled, _control) = drip(
        200,
        Some("application/vnd.apple.mpegurl".to_owned()),
        vec![b"#EXTM3U\n".to_vec()],
    );
    let upstream = MockUpstream::start(vec![("/stalled.m3u8".to_owned(), stalled)])
        .await
        .unwrap();
    let mut owner = runtime_with_config(ProbeHttpConfig {
        connect_timeout: Duration::from_millis(100),
        total_timeout: Duration::from_millis(20),
        allow_private_addresses: true,
        ..ProbeHttpConfig::default()
    });
    let candidate = owner
        .ingest_url(fact(
            upstream.url("/stalled.m3u8"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    let decision = owner
        .decide_for_receiver(
            candidate.id,
            all_capabilities(),
            HandoffAvailability::Available,
        )
        .await
        .unwrap();
    assert_eq!(
        decision.decision,
        CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::ProbeInconclusive
        ))
    );
    assert_eq!(
        upstream.hit_count("/stalled.m3u8"),
        2,
        "HEAD then one Range, no retry"
    );
}

#[tokio::test]
async fn navigation_close_and_ttl_remove_candidates_and_reject_stale_facts() {
    let mut owner = runtime();
    let initial = owner
        .ingest_url(fact(
            "https://media.example/a.mp4".to_owned(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        ))
        .unwrap()
        .unwrap();
    let tab = TabId::new("tab-1").unwrap();
    assert_eq!(owner.on_navigation(&tab, 2), 1);
    assert!(owner.candidates().is_empty());
    assert_eq!(
        owner.ingest_url(fact(
            "https://media.example/late.mp4".to_owned(),
            ObservationSource::NetworkRequest,
            HeadersClass::None,
            None,
        )),
        Err(MediaPlanningError::CandidateUnavailable)
    );

    let mut reopened = fact(
        "https://media.example/b.mp4".to_owned(),
        ObservationSource::CurrentSrc,
        HeadersClass::None,
        Some(playback(120.0)),
    );
    reopened.navigation_id = 2;
    reopened.observed_at_ms = 200;
    let current = owner.ingest_url(reopened).unwrap().unwrap();
    assert_eq!(owner.on_tab_close(&tab), 1);
    assert!(owner.candidates().is_empty());
    assert_eq!(
        owner
            .decide_for_receiver(
                current.id,
                all_capabilities(),
                HandoffAvailability::Available,
            )
            .await,
        Err(MediaPlanningError::CandidateUnavailable)
    );

    let mut newer = fact(
        "https://media.example/c.mp4".to_owned(),
        ObservationSource::CurrentSrc,
        HeadersClass::None,
        Some(playback(120.0)),
    );
    newer.navigation_id = 3;
    newer.observed_at_ms = 300;
    owner.ingest_url(newer).unwrap();
    assert_eq!(owner.expire_stale(311, LifecyclePolicy::new(10)), 1);
    assert!(owner.candidates().is_empty());
    let _ = initial;
}

#[test]
fn owner_sidecar_stays_bounded_with_candidate_eviction() {
    let mut owner = runtime();
    for index in 0..300 {
        let mut observation = fact(
            format!("https://media.example/{index}.mp4"),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(playback(120.0)),
        );
        observation.observed_at_ms = index;
        owner.ingest_url(observation).unwrap();
    }
    assert_eq!(owner.retained_count(), 256);
    assert_eq!(owner.candidates().len(), 256);
}

#[test]
fn candidate_order_reuses_deterministic_med_ranking() {
    let mut owner = runtime();
    let mut small = playback(120.0);
    small.ranking = RankingSignals::new(true, true, false, true, 100);
    let small_candidate = owner
        .ingest_url(fact(
            "https://small.example/a.mp4".to_owned(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(small),
        ))
        .unwrap()
        .unwrap();
    let mut large = playback(120.0);
    large.ranking = RankingSignals::new(true, true, true, true, 10_000);
    let large_candidate = owner
        .ingest_url(fact(
            "https://large.example/b.mp4".to_owned(),
            ObservationSource::CurrentSrc,
            HeadersClass::None,
            Some(large),
        ))
        .unwrap()
        .unwrap();
    let ordered = owner.candidates();
    assert_eq!(ordered[0].id, large_candidate.id);
    assert_eq!(ordered[1].id, small_candidate.id);
}
