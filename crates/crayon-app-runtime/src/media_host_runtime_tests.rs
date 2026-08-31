use super::media_host_runtime::{
    MediaHostInterruptAction, MediaHostPendingQueue, MediaHostRuntime, MediaHostRuntimeError,
    MAX_MEDIA_HOST_PENDING_MESSAGES, MAX_MEDIA_HOST_RECENT_REQUESTS, MAX_MEDIA_HOST_TABS,
};
use crayon_domain::{CoreError, ReceiverCapabilities};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyDecision, ExternalClientHandoff, HandoffReason, HeadersClass,
    MediaHostMessage, MediaHostPlayback, MediaHostSource, MediaHostUrlFact, ProtocolKind,
};
use crayon_media_probe::http::{ProbeHttpClient, ProbeHttpConfig};
use crayon_media_probe::MediaInspector;
use test_support::upstream::{MockUpstream, UpstreamScript};

fn runtime() -> MediaHostRuntime {
    MediaHostRuntime::new(MediaInspector::new(ProbeHttpClient::new(ProbeHttpConfig {
        allow_private_addresses: true,
        ..ProbeHttpConfig::default()
    })))
}

fn playback() -> MediaHostPlayback {
    MediaHostPlayback {
        position_ms: 20_000,
        duration_ms: Some(60_000),
        is_live: false,
        ad_continuity: AdContinuity::Preserved,
        current_src: true,
        near_play_event: true,
        audible: true,
        main_frame: true,
        visible_area_px: 100,
    }
}

fn ingest(request_id: &str, media_url: String) -> MediaHostMessage {
    MediaHostMessage::IngestUrl(MediaHostUrlFact {
        request_id: request_id.to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 1,
        generation: 1,
        observed_at_ms: 10,
        page_url: "https://page.example/watch".to_owned(),
        media_url,
        source: MediaHostSource::CurrentSrc,
        headers_class: HeadersClass::None,
        playback: Some(playback()),
        eme_encrypted: false,
    })
}

fn candidate_id(reply: MediaHostMessage) -> u64 {
    match reply {
        MediaHostMessage::CandidateReply {
            candidate_id: Some(candidate_id),
            redacted_origin,
            ..
        } => {
            assert!(!redacted_origin.contains('?'));
            candidate_id
        }
        _ => panic!("expected candidate reply"),
    }
}

#[tokio::test]
async fn clear_candidate_roundtrips_through_unique_planner() {
    let mut bytes = vec![0, 0, 0, 24];
    bytes.extend_from_slice(b"ftypmp42");
    bytes.extend_from_slice(&[0; 32]);
    let upstream = MockUpstream::start(vec![(
        "/clear.mp4?signature=fixture-value".to_owned(),
        UpstreamScript::HeadRejected(Box::new(UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_owned()),
            body: bytes,
        })),
    )])
    .await
    .unwrap();
    let mut host = runtime();
    let candidate = candidate_id(
        host.handle_immediate(ingest(
            "ingest-1",
            upstream.url("/clear.mp4?signature=fixture-value"),
        ))
        .unwrap()
        .unwrap(),
    );
    let prepared = host
        .prepare_decision(MediaHostMessage::Decide {
            request_id: "decide-1".to_owned(),
            candidate_id: candidate,
            now_ms: 11,
            receiver: ReceiverCapabilities::new(true, true, true, true, true, true, 2160),
            handoff_available: true,
        })
        .unwrap();
    let reply = host.execute_decision(prepared).await.unwrap();
    assert!(matches!(
        reply,
        MediaHostMessage::DecisionReply {
            candidate_id: Some(id),
            protocol: Some(ProtocolKind::Mp4),
            decision: CastPolicyDecision::Direct,
            ..
        } if id == candidate
    ));
    assert!(matches!(
        host.prepare_decision(MediaHostMessage::Decide {
            request_id: "decide-expired".to_owned(),
            candidate_id: candidate,
            now_ms: 600_011,
            receiver: ReceiverCapabilities::new(true, true, true, true, true, true, 2160),
            handoff_available: true,
        }),
        Err(MediaHostRuntimeError::CandidateUnavailable)
    ));
}

#[tokio::test]
async fn url_less_and_eme_stay_in_closed_fail_safe_branches() {
    let mut host = runtime();
    host.handle_immediate(MediaHostMessage::Navigation {
        request_id: "nav-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 1,
        generation: 1,
    })
    .unwrap();
    let prepared = host
        .prepare_decision(MediaHostMessage::DecideUrlLess {
            request_id: "url-less-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
            page_url: "https://page.example/watch".to_owned(),
            playback: playback(),
            eme_encrypted: false,
            handoff_available: true,
        })
        .unwrap();
    assert!(matches!(
        host.execute_decision(prepared).await.unwrap(),
        MediaHostMessage::DecisionReply {
            candidate_id: None,
            protocol: None,
            decision: CastPolicyDecision::ExternalClientHandoff(handoff),
            ..
        } if handoff == ExternalClientHandoff::new(HandoffReason::NoDirectUrl)
    ));

    let prepared = host
        .prepare_decision(MediaHostMessage::DecideUrlLess {
            request_id: "url-less-2".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
            page_url: "https://page.example/watch".to_owned(),
            playback: playback(),
            eme_encrypted: true,
            handoff_available: true,
        })
        .unwrap();
    assert!(matches!(
        host.execute_decision(prepared).await.unwrap(),
        MediaHostMessage::DecisionReply {
            decision: CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected
            },
            ..
        }
    ));
}

#[test]
fn duplicate_stale_navigation_close_and_shutdown_fail_closed() {
    let mut host = runtime();
    host.handle_immediate(MediaHostMessage::Navigation {
        request_id: "nav-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 2,
        generation: 2,
    })
    .unwrap();
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: "nav-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 2,
            generation: 2,
        }),
        Err(MediaHostRuntimeError::InvalidState)
    ));
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: "nav-old".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
        }),
        Err(MediaHostRuntimeError::StaleContext)
    ));
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::CloseTab {
            request_id: "close-old".to_owned(),
            tab_id: "tab-1".to_owned(),
            generation: 1,
        }),
        Err(MediaHostRuntimeError::StaleContext)
    ));
    host.handle_immediate(MediaHostMessage::CloseTab {
        request_id: "close-2".to_owned(),
        tab_id: "tab-1".to_owned(),
        generation: 2,
    })
    .unwrap();
    let mut late = match ingest("late-ingest", "https://media.example/late.mp4".to_owned()) {
        MediaHostMessage::IngestUrl(fact) => fact,
        _ => unreachable!(),
    };
    late.navigation_id = 3;
    late.generation = 2;
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::IngestUrl(late)),
        Err(MediaHostRuntimeError::StaleContext)
    ));
    host.handle_immediate(MediaHostMessage::Navigation {
        request_id: "reopen-3".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 3,
        generation: 3,
    })
    .unwrap();
    let mut reopened = match ingest(
        "reopened-ingest",
        "https://media.example/current.mp4".to_owned(),
    ) {
        MediaHostMessage::IngestUrl(fact) => fact,
        _ => unreachable!(),
    };
    reopened.navigation_id = 3;
    reopened.generation = 3;
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::IngestUrl(reopened)),
        Ok(Some(MediaHostMessage::CandidateReply {
            candidate_id: Some(_),
            ..
        }))
    ));
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::Shutdown),
        Ok(None)
    ));
    assert!(host.is_shutdown());
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: "late".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 3,
            generation: 3,
        }),
        Err(MediaHostRuntimeError::HostUnavailable)
    ));
}

#[test]
fn tab_and_recent_request_collections_are_bounded() {
    let mut host = runtime();
    for index in 0..MAX_MEDIA_HOST_TABS {
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: format!("nav-{index}"),
            tab_id: format!("tab-{index}"),
            navigation_id: 1,
            generation: 1,
        })
        .unwrap();
    }
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: "nav-overflow".to_owned(),
            tab_id: "tab-overflow".to_owned(),
            navigation_id: 1,
            generation: 1,
        }),
        Err(MediaHostRuntimeError::CapacityExceeded)
    ));

    let mut host = runtime();
    for index in 0..=MAX_MEDIA_HOST_RECENT_REQUESTS {
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: format!("request-{index}"),
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
        })
        .unwrap();
    }
    assert!(matches!(
        host.handle_immediate(MediaHostMessage::Navigation {
            request_id: "request-0".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
        }),
        Ok(Some(MediaHostMessage::Ack { .. }))
    ));
}

fn queued_decision(request_id: &str) -> MediaHostMessage {
    MediaHostMessage::Decide {
        request_id: request_id.to_owned(),
        candidate_id: 1,
        now_ms: 1,
        receiver: ReceiverCapabilities::new(true, true, true, true, true, true, 2160),
        handoff_available: true,
    }
}

#[test]
fn active_decision_interrupt_and_backpressure_matrix_is_closed() {
    let mut pending = MediaHostPendingQueue::default();
    let (action, reply) = pending
        .accept_during_decision(
            "active-1",
            MediaHostMessage::Cancel {
                request_id: "active-1".to_owned(),
            },
        )
        .unwrap();
    assert_eq!(action, MediaHostInterruptAction::Cancel);
    assert!(reply.is_none());

    let (action, reply) = pending
        .accept_during_decision(
            "active-1",
            MediaHostMessage::Navigation {
                request_id: "nav-2".to_owned(),
                tab_id: "tab-1".to_owned(),
                navigation_id: 2,
                generation: 2,
            },
        )
        .unwrap();
    assert_eq!(action, MediaHostInterruptAction::Cancel);
    assert!(reply.is_none());
    assert!(matches!(
        pending.pop_front(),
        Some(MediaHostMessage::Navigation {
            navigation_id: 2,
            ..
        })
    ));

    let (action, reply) = pending
        .accept_during_decision("active-1", MediaHostMessage::Shutdown)
        .unwrap();
    assert_eq!(action, MediaHostInterruptAction::Shutdown);
    assert!(reply.is_none());

    for index in 0..MAX_MEDIA_HOST_PENDING_MESSAGES {
        let (action, reply) = pending
            .accept_during_decision("active-1", queued_decision(&format!("queued-{index}")))
            .unwrap();
        assert_eq!(action, MediaHostInterruptAction::Continue);
        assert!(reply.is_none());
    }
    assert_eq!(pending.len(), MAX_MEDIA_HOST_PENDING_MESSAGES);
    let (action, reply) = pending
        .accept_during_decision("active-1", queued_decision("overflow"))
        .unwrap();
    assert_eq!(action, MediaHostInterruptAction::Continue);
    assert_eq!(pending.len(), MAX_MEDIA_HOST_PENDING_MESSAGES);
    assert!(matches!(
        reply,
        Some(MediaHostMessage::ErrorReply {
            request_id,
            code: crayon_ipc_schema::MediaHostErrorCode::CapacityExceeded,
        }) if request_id == "overflow"
    ));

    let (action, reply) = pending
        .accept_during_decision(
            "active-1",
            MediaHostMessage::Navigation {
                request_id: "urgent-navigation".to_owned(),
                tab_id: "tab-1".to_owned(),
                navigation_id: 2,
                generation: 2,
            },
        )
        .unwrap();
    assert_eq!(action, MediaHostInterruptAction::Cancel);
    assert_eq!(pending.len(), MAX_MEDIA_HOST_PENDING_MESSAGES);
    assert!(matches!(
        reply,
        Some(MediaHostMessage::ErrorReply {
            code: crayon_ipc_schema::MediaHostErrorCode::CapacityExceeded,
            ..
        })
    ));
}
