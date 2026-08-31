use crayon_domain::{CoreError, ReceiverCapabilities};
use crayon_ipc_schema::{
    decode_media_host_message, encode_media_host_message, AdContinuity, CastPolicyDecision,
    ExternalClientHandoff, HandoffReason, HeadersClass, MediaHostCastControlAction,
    MediaHostCastControlOutcome, MediaHostCastErrorCode, MediaHostCastStartOutcome,
    MediaHostDeliveryRoute, MediaHostDevice, MediaHostDeviceState, MediaHostDiscoveryAction,
    MediaHostError, MediaHostErrorCode, MediaHostMessage, MediaHostPlayback,
    MediaHostResolveCastCodeOutcome, MediaHostSessionEvent, MediaHostSessionPhase,
    MediaHostSessionPlayback, MediaHostSource, MediaHostTerminalReason, MediaHostUrlFact,
    ProtocolKind, MAX_MEDIA_HOST_FRAME_BYTES, MAX_MEDIA_HOST_SEEK_SECONDS,
};

const CURRENT_INGEST_GOLDEN: &str = "4d4856310001010000000009726571756573742d31000000057461622d3100000000000000070000000000000009000000000000007b0000001a68747470733a2f2f706167652e6578616d706c652f77617463680000003768747470733a2f2f6d656469612e6578616d706c652f766964656f2e6d70343f7369676e61747572653d666978747572652d76616c7565000001000000000000303901000000000000ea60000001010101000e100000";
// MHV1 has not changed yet; the previous compatibility vector is deliberately
// byte-identical until a v2 writer exists.
const PREVIOUS_INGEST_GOLDEN: &str = CURRENT_INGEST_GOLDEN;
const RESOLVE_CAST_CODE_GOLDEN: &str = "4d4856310001150000000006636f64652d310000000741423120434432";
const RESOLVE_CAST_CODE_REPLY_GOLDEN: &str = "4d485631000116000000000b636f64652d6661696c65640100";
const CONTROL_CAST_GOLDEN: &str =
    "4d48563100011700000000067365656b2d31000000000000000b0201000000000000001e";
const CONTROL_CAST_REPLY_GOLDEN: &str =
    "4d485631000118000000000b7365656b2d6661696c6564000000000000000b0108";

fn playback() -> MediaHostPlayback {
    MediaHostPlayback {
        position_ms: 12_345,
        duration_ms: Some(60_000),
        is_live: false,
        ad_continuity: AdContinuity::Preserved,
        current_src: true,
        near_play_event: true,
        audible: true,
        main_frame: true,
        visible_area_px: 921_600,
    }
}

fn ingest() -> MediaHostMessage {
    MediaHostMessage::IngestUrl(MediaHostUrlFact {
        request_id: "request-1".to_owned(),
        tab_id: "tab-1".to_owned(),
        navigation_id: 7,
        generation: 9,
        observed_at_ms: 123,
        page_url: "https://page.example/watch".to_owned(),
        media_url: "https://media.example/video.mp4?signature=fixture-value".to_owned(),
        source: MediaHostSource::CurrentSrc,
        headers_class: HeadersClass::None,
        playback: Some(playback()),
        eme_encrypted: false,
    })
}

fn all_messages() -> Vec<MediaHostMessage> {
    vec![
        ingest(),
        MediaHostMessage::MarkEme {
            request_id: "eme-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 7,
            generation: 9,
        },
        MediaHostMessage::Decide {
            request_id: "decide-1".to_owned(),
            candidate_id: 3,
            now_ms: 124,
            receiver: ReceiverCapabilities::new(true, true, false, true, false, false, 1080),
            handoff_available: true,
        },
        MediaHostMessage::DecideUrlLess {
            request_id: "url-less-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 7,
            generation: 9,
            page_url: "https://page.example/watch".to_owned(),
            playback: playback(),
            eme_encrypted: false,
            handoff_available: true,
        },
        MediaHostMessage::Cancel {
            request_id: "decide-1".to_owned(),
        },
        MediaHostMessage::Navigation {
            request_id: "nav-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            navigation_id: 8,
            generation: 10,
        },
        MediaHostMessage::CloseTab {
            request_id: "close-1".to_owned(),
            tab_id: "tab-1".to_owned(),
            generation: 10,
        },
        MediaHostMessage::Shutdown,
        MediaHostMessage::CandidateReply {
            request_id: "request-1".to_owned(),
            candidate_id: Some(3),
            redacted_origin: "https://media.example".to_owned(),
        },
        MediaHostMessage::CandidateReply {
            request_id: "network-1".to_owned(),
            candidate_id: None,
            redacted_origin: String::new(),
        },
        MediaHostMessage::DecisionReply {
            request_id: "decide-1".to_owned(),
            candidate_id: Some(3),
            protocol: Some(ProtocolKind::Mp4),
            decision: CastPolicyDecision::Direct,
        },
        MediaHostMessage::DecisionReply {
            request_id: "url-less-1".to_owned(),
            candidate_id: None,
            protocol: None,
            decision: CastPolicyDecision::ExternalClientHandoff(ExternalClientHandoff::new(
                HandoffReason::NoDirectUrl,
            )),
        },
        MediaHostMessage::DecisionReply {
            request_id: "drm-1".to_owned(),
            candidate_id: Some(4),
            protocol: Some(ProtocolKind::Dash),
            decision: CastPolicyDecision::Reject {
                reason: CoreError::DrmProtected,
            },
        },
        MediaHostMessage::Ack {
            request_id: "nav-1".to_owned(),
        },
        MediaHostMessage::ErrorReply {
            request_id: "bad-1".to_owned(),
            code: MediaHostErrorCode::StaleContext,
        },
        MediaHostMessage::Discovery {
            request_id: "discover-1".to_owned(),
            action: MediaHostDiscoveryAction::Refresh,
        },
        MediaHostMessage::ListDevices {
            request_id: "devices-1".to_owned(),
            snapshot_revision: None,
            offset: 0,
        },
        device_page(),
        MediaHostMessage::StartCast {
            request_id: "cast-1".to_owned(),
            candidate_id: 7,
            device_id: "receiver_1".to_owned(),
            handoff_available: true,
        },
        MediaHostMessage::StartCastReply {
            request_id: "cast-1".to_owned(),
            outcome: MediaHostCastStartOutcome::Casting {
                session_generation: 11,
                route: MediaHostDeliveryRoute::Relay,
            },
        },
        MediaHostMessage::StartCastReply {
            request_id: "cast-failed".to_owned(),
            outcome: MediaHostCastStartOutcome::Failed {
                code: MediaHostCastErrorCode::ReceiverUnreachable,
            },
        },
        MediaHostMessage::StopCast {
            request_id: "stop-1".to_owned(),
            session_generation: 11,
        },
        MediaHostMessage::PollSessionEvents {
            request_id: "events-1".to_owned(),
        },
        MediaHostMessage::SessionEventsReply {
            request_id: "events-1".to_owned(),
            dropped_events: 2,
            events: vec![
                MediaHostSessionEvent {
                    session_generation: 11,
                    state_revision: 3,
                    phase: MediaHostSessionPhase::Active,
                    playback: MediaHostSessionPlayback::Playing,
                    terminal_reason: None,
                },
                MediaHostSessionEvent {
                    session_generation: 11,
                    state_revision: 4,
                    phase: MediaHostSessionPhase::Terminated,
                    playback: MediaHostSessionPlayback::Stopped,
                    terminal_reason: Some(MediaHostTerminalReason::StoppedBySender),
                },
            ],
        },
        MediaHostMessage::ResolveCastCode {
            request_id: "code-1".to_owned(),
            cast_code: "AB1 CD2".to_owned(),
        },
        MediaHostMessage::ResolveCastCodeReply {
            request_id: "code-1".to_owned(),
            outcome: MediaHostResolveCastCodeOutcome::Resolved(MediaHostDevice {
                device_id: "receiver_1".to_owned(),
                display_name: "Living Room".to_owned(),
                state: MediaHostDeviceState::Ready,
                is_crayon_receiver: true,
            }),
        },
        MediaHostMessage::ResolveCastCodeReply {
            request_id: "code-failed".to_owned(),
            outcome: MediaHostResolveCastCodeOutcome::Failed(
                MediaHostCastErrorCode::DeviceNotFound,
            ),
        },
        MediaHostMessage::ControlCast {
            request_id: "pause-1".to_owned(),
            session_generation: 11,
            action: MediaHostCastControlAction::Pause,
            position_seconds: None,
        },
        MediaHostMessage::ControlCast {
            request_id: "seek-1".to_owned(),
            session_generation: 11,
            action: MediaHostCastControlAction::Seek,
            position_seconds: Some(30),
        },
        MediaHostMessage::ControlCastReply {
            request_id: "pause-1".to_owned(),
            session_generation: 11,
            outcome: MediaHostCastControlOutcome::Applied,
        },
        MediaHostMessage::ControlCastReply {
            request_id: "seek-failed".to_owned(),
            session_generation: 11,
            outcome: MediaHostCastControlOutcome::Failed(MediaHostCastErrorCode::RouteLost),
        },
    ]
}

fn device_page() -> MediaHostMessage {
    MediaHostMessage::DevicePageReply {
        request_id: "devices-1".to_owned(),
        snapshot_revision: 5,
        offset: 0,
        next_offset: None,
        devices: vec![MediaHostDevice {
            device_id: "receiver_1".to_owned(),
            display_name: "Living Room".to_owned(),
            state: MediaHostDeviceState::Ready,
            is_crayon_receiver: true,
        }],
    }
}

#[test]
fn current_and_previous_vectors_roundtrip() {
    for message in all_messages() {
        let encoded = encode_media_host_message(&message).unwrap();
        let decoded = decode_media_host_message(&encoded).unwrap();
        assert!(decoded == message);
    }

    let current = encode_media_host_message(&ingest()).unwrap();
    let previous = current.clone();
    assert_eq!(hex(&current), CURRENT_INGEST_GOLDEN);
    assert_eq!(hex(&previous), PREVIOUS_INGEST_GOLDEN);
    assert!(decode_media_host_message(&previous).unwrap() == ingest());

    const CAST_GOLDEN: &str = "4d48563100010f0000000009646576696365732d3100000000000000050000ffff00010000000a72656365697665725f310000000b4c6976696e6720526f6f6d0001";
    assert_eq!(
        hex(&encode_media_host_message(&device_page()).unwrap()),
        CAST_GOLDEN
    );
    assert_eq!(
        hex(
            &encode_media_host_message(&MediaHostMessage::ResolveCastCode {
                request_id: "code-1".to_owned(),
                cast_code: "AB1 CD2".to_owned(),
            })
            .unwrap()
        ),
        RESOLVE_CAST_CODE_GOLDEN
    );
    assert_eq!(
        hex(
            &encode_media_host_message(&MediaHostMessage::ResolveCastCodeReply {
                request_id: "code-failed".to_owned(),
                outcome: MediaHostResolveCastCodeOutcome::Failed(
                    MediaHostCastErrorCode::DeviceNotFound,
                ),
            })
            .unwrap()
        ),
        RESOLVE_CAST_CODE_REPLY_GOLDEN
    );
    assert_eq!(
        hex(&encode_media_host_message(&MediaHostMessage::ControlCast {
            request_id: "seek-1".to_owned(),
            session_generation: 11,
            action: MediaHostCastControlAction::Seek,
            position_seconds: Some(30),
        })
        .unwrap()),
        CONTROL_CAST_GOLDEN
    );
    assert_eq!(
        hex(
            &encode_media_host_message(&MediaHostMessage::ControlCastReply {
                request_id: "seek-failed".to_owned(),
                session_generation: 11,
                outcome: MediaHostCastControlOutcome::Failed(MediaHostCastErrorCode::RouteLost),
            })
            .unwrap()
        ),
        CONTROL_CAST_REPLY_GOLDEN
    );
}

#[test]
fn malformed_truncated_oversize_and_unknown_inputs_fail_closed() {
    let encoded = encode_media_host_message(&ingest()).unwrap();
    for length in 0..encoded.len() {
        assert!(decode_media_host_message(&encoded[..length]).is_err());
    }
    let mut malformed = encoded.clone();
    malformed[0] = b'X';
    assert!(matches!(
        decode_media_host_message(&malformed),
        Err(MediaHostError::InvalidMagic)
    ));
    malformed = encoded.clone();
    malformed[4..6].copy_from_slice(&2u16.to_be_bytes());
    assert!(matches!(
        decode_media_host_message(&malformed),
        Err(MediaHostError::UnsupportedVersion)
    ));
    malformed = encoded.clone();
    malformed[6] = 0xff;
    assert!(matches!(
        decode_media_host_message(&malformed),
        Err(MediaHostError::UnknownKind)
    ));
    malformed = encoded;
    malformed[7] = 1;
    assert!(matches!(
        decode_media_host_message(&malformed),
        Err(MediaHostError::InvalidFlags)
    ));
    assert!(matches!(
        decode_media_host_message(&vec![0; MAX_MEDIA_HOST_FRAME_BYTES + 1]),
        Err(MediaHostError::FrameTooLarge)
    ));
}

#[test]
fn bounds_invalid_shapes_and_hostile_mutations_are_rejected() {
    let mut invalid = match ingest() {
        MediaHostMessage::IngestUrl(fact) => fact,
        _ => unreachable!(),
    };
    invalid.request_id = "x".repeat(129);
    assert!(encode_media_host_message(&MediaHostMessage::IngestUrl(invalid)).is_err());

    let mut invalid = match ingest() {
        MediaHostMessage::IngestUrl(fact) => fact,
        _ => unreachable!(),
    };
    invalid.playback.as_mut().unwrap().position_ms = 9_007_199_254_740_993;
    assert!(encode_media_host_message(&MediaHostMessage::IngestUrl(invalid)).is_err());

    assert!(
        encode_media_host_message(&MediaHostMessage::CandidateReply {
            request_id: "r-1".to_owned(),
            candidate_id: None,
            redacted_origin: "https://media.example".to_owned(),
        })
        .is_err()
    );
    assert!(encode_media_host_message(&MediaHostMessage::DecisionReply {
        request_id: "r-2".to_owned(),
        candidate_id: Some(1),
        protocol: None,
        decision: CastPolicyDecision::Direct,
    })
    .is_err());

    assert!(encode_media_host_message(&MediaHostMessage::ListDevices {
        request_id: "devices-invalid".to_owned(),
        snapshot_revision: None,
        offset: 16,
    })
    .is_err());
    assert!(
        encode_media_host_message(&MediaHostMessage::DevicePageReply {
            request_id: "devices-invalid".to_owned(),
            snapshot_revision: 1,
            offset: 0,
            next_offset: Some(2),
            devices: vec![MediaHostDevice {
                device_id: "receiver/invalid".to_owned(),
                display_name: "Living Room".to_owned(),
                state: MediaHostDeviceState::Ready,
                is_crayon_receiver: true,
            }],
        })
        .is_err()
    );
    let duplicate = MediaHostDevice {
        device_id: "receiver_1".to_owned(),
        display_name: "Duplicate".to_owned(),
        state: MediaHostDeviceState::Ready,
        is_crayon_receiver: false,
    };
    let mut devices = match device_page() {
        MediaHostMessage::DevicePageReply { devices, .. } => devices,
        _ => unreachable!(),
    };
    devices.push(duplicate);
    assert!(
        encode_media_host_message(&MediaHostMessage::DevicePageReply {
            request_id: "devices-duplicate".to_owned(),
            snapshot_revision: 1,
            offset: 0,
            next_offset: None,
            devices,
        })
        .is_err()
    );
    assert!(
        encode_media_host_message(&MediaHostMessage::StartCastReply {
            request_id: "cast-invalid".to_owned(),
            outcome: MediaHostCastStartOutcome::Casting {
                session_generation: 0,
                route: MediaHostDeliveryRoute::Direct,
            },
        })
        .is_err()
    );
    assert!(
        encode_media_host_message(&MediaHostMessage::SessionEventsReply {
            request_id: "events-invalid".to_owned(),
            dropped_events: 0,
            events: vec![MediaHostSessionEvent {
                session_generation: 1,
                state_revision: 1,
                phase: MediaHostSessionPhase::Terminated,
                playback: MediaHostSessionPlayback::Stopped,
                terminal_reason: None,
            }],
        })
        .is_err()
    );
    assert!(
        encode_media_host_message(&MediaHostMessage::ResolveCastCode {
            request_id: "code-invalid".to_owned(),
            cast_code: "ABC/123".to_owned(),
        })
        .is_err()
    );
    assert!(encode_media_host_message(&MediaHostMessage::ControlCast {
        request_id: "control-invalid".to_owned(),
        session_generation: 0,
        action: MediaHostCastControlAction::Play,
        position_seconds: None,
    })
    .is_err());
    assert!(encode_media_host_message(&MediaHostMessage::ControlCast {
        request_id: "control-invalid".to_owned(),
        session_generation: 1,
        action: MediaHostCastControlAction::Pause,
        position_seconds: Some(1),
    })
    .is_err());
    assert!(encode_media_host_message(&MediaHostMessage::ControlCast {
        request_id: "control-invalid".to_owned(),
        session_generation: 1,
        action: MediaHostCastControlAction::Seek,
        position_seconds: Some(MAX_MEDIA_HOST_SEEK_SECONDS + 1),
    })
    .is_err());
    let mut invalid_control_reply =
        encode_media_host_message(&MediaHostMessage::ControlCastReply {
            request_id: "control-invalid".to_owned(),
            session_generation: 1,
            outcome: MediaHostCastControlOutcome::Applied,
        })
        .unwrap();
    *invalid_control_reply.last_mut().unwrap() = 2;
    assert!(decode_media_host_message(&invalid_control_reply).is_err());

    let seed = encode_media_host_message(&ingest()).unwrap();
    for index in 0..seed.len() {
        let mut mutated = seed.clone();
        mutated[index] ^= 0xa5;
        let _ = decode_media_host_message(&mutated);
    }
    let cast_seed = encode_media_host_message(&device_page()).unwrap();
    for length in 0..cast_seed.len() {
        assert!(decode_media_host_message(&cast_seed[..length]).is_err());
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}
