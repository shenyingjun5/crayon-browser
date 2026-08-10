//! SDK-03 facade contract tests: serde roundtrip/goldens, the CS-008 stable
//! error mapping table, sensitive-field wire assertions, fencing semantics,
//! and compile-only object-safety checks. No SDK service is constructed and
//! no network is touched.

use crayon_cast_adapter::{
    AssessmentStatus, CastCode, CastError, CastFacade, CastMediaKind, CastMediaRequest,
    CastMediaUrl, CastPlaybackState, CastSessionPhase, CastSessionRef, CastSessionSnapshot,
    CastTerminalReason, DeliveryProtocol, DeviceState, DiscoveredDevice, PlaybackPosition,
    ReceiverAssessment, SenderErrorKind, Volume,
};
use crayon_domain::{DeviceId, SessionGeneration, SessionId};
use serde_json::{json, Value};

fn device_id(raw: &str) -> DeviceId {
    DeviceId::new(raw).expect("valid device id")
}

fn session_ref(id: &str, generation: u64) -> CastSessionRef {
    CastSessionRef::new(
        SessionId::new(id).expect("valid session id"),
        SessionGeneration::from_raw(generation),
    )
}

fn roundtrip_value<T>(value: &T) -> Value
where
    T: serde::Serialize + serde::de::DeserializeOwned + std::fmt::Debug + PartialEq,
{
    let wire = serde_json::to_value(value).expect("serialize");
    let back: T = serde_json::from_value(wire.clone()).expect("deserialize");
    assert_eq!(&back, value);
    wire
}

// -- CS-008: stable error mapping table ------------------------------------

#[test]
fn cs_008_mapping_table() {
    let cases: &[(SenderErrorKind, &str, CastError)] = &[
        (
            SenderErrorKind::Device,
            "DEVICE_NOT_FOUND",
            CastError::DeviceNotFound,
        ),
        (
            SenderErrorKind::Device,
            "CAST_CODE_DEVICE_NOT_FOUND",
            CastError::DeviceNotFound,
        ),
        (
            SenderErrorKind::Network,
            "SENDER_DEVICE_ROUTE_EXPIRED",
            CastError::RouteLost,
        ),
        (
            SenderErrorKind::Network,
            "NETWORK_ROUTE_LOST",
            CastError::RouteLost,
        ),
        (
            SenderErrorKind::Network,
            "NETWORK_ROUTE_TEMPORARILY_UNAVAILABLE",
            CastError::RouteLost,
        ),
        (
            SenderErrorKind::Network,
            "NO_USABLE_LAN_INTERFACE",
            CastError::NetworkUnavailable,
        ),
        (
            SenderErrorKind::Http,
            "DESCRIPTION_FETCH_FAILED",
            CastError::ReceiverUnreachable,
        ),
        (
            SenderErrorKind::Image,
            "IMAGE_PROCESS_FAILED",
            CastError::UnsupportedByReceiver,
        ),
        (
            SenderErrorKind::Control,
            "CONTROL_CAST_EXTENSION_MISSING",
            CastError::UnsupportedByReceiver,
        ),
        (
            SenderErrorKind::Control,
            "SOAP_WRITE_FAILED",
            CastError::ReceiverProtocol,
        ),
        (
            SenderErrorKind::InvalidInput,
            "SENDER_INVALID_INPUT",
            CastError::InvalidInput,
        ),
        (
            SenderErrorKind::State,
            "CAST_SESSION_STALE_GENERATION",
            CastError::StaleSessionGeneration,
        ),
        (
            SenderErrorKind::State,
            "CAST_SESSION_NOT_FOUND",
            CastError::NoActiveSession,
        ),
        (
            SenderErrorKind::State,
            "CAST_SESSION_ALREADY_TERMINATED",
            CastError::NoActiveSession,
        ),
        (
            SenderErrorKind::State,
            "CAST_SESSION_START_FAILED",
            CastError::CastStartFailed,
        ),
        (
            SenderErrorKind::State,
            "SENDER_INVALID_STATE",
            CastError::InvalidState,
        ),
    ];
    for (kind, code, expected) in cases {
        assert_eq!(
            CastError::from_sender_error(*kind, code),
            *expected,
            "{kind:?}/{code}"
        );
    }
}

#[test]
fn cs_008_every_sender_error_kind_maps() {
    // Every category reaches a stable product error even for unknown codes.
    assert_eq!(SenderErrorKind::ALL.len(), 7);
    for kind in SenderErrorKind::ALL {
        let _ = CastError::from_sender_error(*kind, "UNRECOGNIZED_FUTURE_CODE");
    }
}

#[test]
fn cs_008_error_codes_are_stable() {
    let expected = [
        "device_not_found",
        "invalid_cast_code",
        "invalid_input",
        "invalid_state",
        "no_active_session",
        "stale_session_generation",
        "cast_start_failed",
        "unsupported_by_receiver",
        "route_lost",
        "network_unavailable",
        "receiver_unreachable",
        "receiver_protocol",
        "internal",
    ];
    let actual: Vec<&str> = CastError::ALL.iter().map(|e| e.code()).collect();
    assert_eq!(actual, expected);
}

#[test]
fn cs_008_error_serde_and_display_contract() {
    for error in CastError::ALL {
        let wire = serde_json::to_string(error).expect("serialize");
        assert_eq!(wire, format!("\"{}\"", error.code()));
        assert_eq!(error.to_string(), error.code());
        let back: CastError = serde_json::from_str(&wire).expect("deserialize");
        assert_eq!(&back, error);
        assert_eq!(CastError::from_code(error.code()), Some(*error));
    }
    assert!(serde_json::from_str::<CastError>("\"not_a_code\"").is_err());
    assert_eq!(CastError::from_code("not_a_code"), None);
}

// -- DTO serde roundtrip / golden -------------------------------------------

#[test]
fn device_state_and_enums_roundtrip() {
    for (value, wire) in [
        (json!(DeviceState::Ready), json!("ready")),
        (
            json!(DeviceState::RequiresAuthorization),
            json!("requires_authorization"),
        ),
        (json!(CastMediaKind::Video), json!("video")),
        (json!(CastMediaKind::Hls), json!("hls")),
        (json!(AssessmentStatus::Risky), json!("risky")),
        (json!(DeliveryProtocol::Mp4), json!("mp4")),
        (json!(DeliveryProtocol::Hls), json!("hls")),
        (json!(CastSessionPhase::Terminating), json!("terminating")),
        (json!(CastPlaybackState::Buffering), json!("buffering")),
        (
            json!(CastTerminalReason::ReceiverSessionLost),
            json!("receiver_session_lost"),
        ),
        (json!(SenderErrorKind::InvalidInput), json!("invalid_input")),
    ] {
        assert_eq!(value, wire);
    }
}

#[test]
fn discovered_device_golden_and_wire_keys() {
    let device = DiscoveredDevice::new(
        device_id("9f3ab2c1d4e5f607"),
        "客厅电视".to_string(),
        DeviceState::Ready,
        true,
    );
    let wire = roundtrip_value(&device);
    assert_eq!(
        wire,
        json!({
            "device_id": "9f3ab2c1d4e5f607",
            "friendly_name": "客厅电视",
            "state": "ready",
            "is_crayon_receiver": true,
        })
    );
}

/// CS-002/AG-007: the device snapshot must never expose a network locator.
#[test]
fn discovered_device_wire_has_no_network_locator() {
    let device = DiscoveredDevice::new(
        device_id("9f3ab2c1d4e5f607"),
        "192.168.1.20".to_string(),
        DeviceState::Ready,
        false,
    );
    let wire = serde_json::to_string(&device).expect("serialize");
    let object = serde_json::from_str::<Value>(&wire).expect("json");
    let mut keys: Vec<&str> = object
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
    for forbidden in ["host", "ip", "port", "location", "udn", "url", "address"] {
        assert!(
            !keys.iter().any(|key| key.contains(forbidden)),
            "forbidden locator key: {forbidden}"
        );
    }
}

#[test]
fn receiver_assessment_roundtrip() {
    let assessment = ReceiverAssessment::new(
        device_id("9f3ab2c1d4e5f607"),
        CastMediaKind::Hls,
        AssessmentStatus::Supported,
    );
    assert_eq!(
        roundtrip_value(&assessment),
        json!({
            "device_id": "9f3ab2c1d4e5f607",
            "media": "hls",
            "status": "supported",
        })
    );
}

#[test]
fn playback_position_never_carries_track_uri() {
    let position = PlaybackPosition::new(Some(42), Some(3600));
    let wire = roundtrip_value(&position);
    assert_eq!(
        wire,
        json!({"position_seconds": 42, "duration_seconds": 3600})
    );
    let empty = PlaybackPosition::new(None, None);
    assert_eq!(
        roundtrip_value(&empty),
        json!({"position_seconds": null, "duration_seconds": null})
    );
}

#[test]
fn session_snapshot_roundtrip_and_terminal_reason() {
    let snapshot = CastSessionSnapshot::new(
        session_ref("cast-abc123", 7),
        CastSessionPhase::Terminated,
        CastPlaybackState::Stopped,
        12,
        Some(CastTerminalReason::StoppedByReceiver),
    );
    let wire = roundtrip_value(&snapshot);
    assert_eq!(
        wire,
        json!({
            "session": {"session_id": "cast-abc123", "generation": 7},
            "phase": "terminated",
            "playback": "stopped",
            "state_revision": 12,
            "terminal_reason": "stopped_by_receiver",
        })
    );
    assert!(snapshot.is_terminal());
}

// -- Fencing semantics (CS-006/CS-007) ---------------------------------------

#[test]
fn snapshot_supersedes_fencing_matrix() {
    let base = || {
        CastSessionSnapshot::new(
            session_ref("cast-a", 2),
            CastSessionPhase::Active,
            CastPlaybackState::Playing,
            5,
            None,
        )
    };
    // Newer generation always wins, even with a lower revision.
    let newer_gen = CastSessionSnapshot::new(
        session_ref("cast-b", 3),
        CastSessionPhase::Starting,
        CastPlaybackState::Preparing,
        1,
        None,
    );
    assert!(newer_gen.supersedes(&base()));
    assert!(!base().supersedes(&newer_gen));
    // Same session + generation: only a higher revision wins.
    let newer_rev = CastSessionSnapshot::new(
        session_ref("cast-a", 2),
        CastSessionPhase::Active,
        CastPlaybackState::Paused,
        6,
        None,
    );
    assert!(newer_rev.supersedes(&base()));
    assert!(!base().supersedes(&newer_rev));
    // Same generation but a different session id must not supersede.
    let other_session = CastSessionSnapshot::new(
        session_ref("cast-b", 2),
        CastSessionPhase::Active,
        CastPlaybackState::Playing,
        9,
        None,
    );
    assert!(!other_session.supersedes(&base()));
    // An event never supersedes itself (replayed delivery is dropped).
    assert!(!base().supersedes(&base()));
}

// -- Input validation & sensitive-field handling ------------------------------

#[test]
fn cs_003_cast_code_validation() {
    assert_eq!(
        CastCode::new("ab-cd12").expect("normalized").as_str(),
        "ABCD12"
    );
    assert_eq!(
        CastCode::new(" abcd12 ").expect("trimmed").as_str(),
        "ABCD12"
    );
    for bad in ["", "ABCD1", "ABCD123", "ABCD一2", "AB CD"] {
        assert_eq!(
            CastCode::new(bad),
            Err(CastError::InvalidCastCode),
            "input {bad:?}"
        );
    }
    // Wire form is the normalized string; malformed wire input is rejected.
    assert_eq!(
        serde_json::to_string(&CastCode::new("abcd12").expect("valid")).expect("serialize"),
        "\"ABCD12\""
    );
    assert!(serde_json::from_str::<CastCode>("\"!!\"").is_err());
}

#[test]
fn volume_bounds_match_pinned_sdk() {
    assert_eq!(Volume::new(0).expect("min").get(), 0);
    assert_eq!(Volume::new(100).expect("max").get(), 100);
    assert_eq!(Volume::new(101), Err(CastError::InvalidInput));
    assert_eq!(
        serde_json::to_string(&Volume::new(55).expect("valid")).expect("serialize"),
        "55"
    );
    assert!(serde_json::from_str::<Volume>("101").is_err());
}

#[test]
fn media_url_boundary_validation() {
    assert!(CastMediaUrl::new("http://receiver.local/media.mp4").is_ok());
    assert!(CastMediaUrl::new("https://cdn.example.com/v.m3u8?sig=abc").is_ok());
    for bad in [
        "",
        "ftp://example.com/v.mp4",
        "file:///etc/passwd",
        "javascript:alert(1)",
        "//example.com/schemeless",
    ] {
        assert_eq!(
            CastMediaUrl::new(bad),
            Err(CastError::InvalidInput),
            "input {bad:?}"
        );
    }
    let oversized = format!("http://example.com/{}", "a".repeat(4096));
    assert_eq!(CastMediaUrl::new(&oversized), Err(CastError::InvalidInput));
}

/// RL-014: the delivery request and its URL never leak through `Debug`, and
/// neither type is serializable (this file cannot even name a serializer for
/// them — the absence of `Serialize` is the contract).
#[test]
fn media_url_debug_is_redacted() {
    let url = CastMediaUrl::new("https://cdn.example.com/v.m3u8?sig=secret-token").expect("valid");
    assert_eq!(format!("{url:?}"), "CastMediaUrl(REDACTED)");
    let request = CastMediaRequest::new(device_id("9f3ab2c1d4e5f607"), DeliveryProtocol::Hls, url);
    let debug = format!("{request:?}");
    assert!(!debug.contains("sig=secret-token"));
    assert!(!debug.contains("cdn.example.com"));
    assert!(debug.contains("REDACTED"));
}

// -- Compile-only trait contract ----------------------------------------------

/// The facade must stay object-safe for `Arc<dyn CastFacade>` consumers
/// (app-runtime, SDK-04 fake).
#[allow(dead_code)]
fn assert_facade_object_safety(_: &dyn CastFacade) {}

#[test]
fn session_listener_blanket_impl_accepts_closures() {
    use crayon_cast_adapter::CastSessionListener;
    use std::sync::{Arc, Mutex};

    let seen = Arc::new(Mutex::new(Vec::new()));
    let seen_clone = Arc::clone(&seen);
    let listener = move |snapshot: CastSessionSnapshot| {
        seen_clone
            .lock()
            .expect("seen poisoned")
            .push(snapshot.phase());
    };
    let listener: Arc<dyn CastSessionListener> = Arc::new(listener);
    let snapshot = CastSessionSnapshot::new(
        session_ref("cast-a", 1),
        CastSessionPhase::Active,
        CastPlaybackState::Playing,
        1,
        None,
    );
    listener.on_session_changed(snapshot);
    assert_eq!(
        seen.lock().expect("seen poisoned").as_slice(),
        &[CastSessionPhase::Active]
    );
}
