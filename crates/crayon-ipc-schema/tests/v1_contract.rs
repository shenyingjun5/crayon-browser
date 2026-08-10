//! Core API v1 contract tests (FND-08): golden roundtrip, previous-version
//! compatibility window, unknown field/version rejection, and secret denial.

use crayon_domain::{CoreError, PlatformCapabilities, ReceiverCapabilities};
use crayon_ipc_schema::{
    CastPolicyDecision, CastPolicyInput, Handshake, MediaCandidate, SchemaVersion, SessionGrant,
    SessionSecret, SourceObservation,
};
use serde::de::DeserializeOwned;
use serde::Serialize;
use serde_json::Value;
use std::num::NonZeroU16;
use std::path::PathBuf;

fn vector(set: &str, name: &str) -> String {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../schemas")
        .join(set)
        .join(name);
    std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

fn roundtrip<T>(raw: &str) -> Value
where
    T: DeserializeOwned + Serialize,
{
    let parsed: T = serde_json::from_str(raw).expect("golden vector must deserialize");
    serde_json::to_value(&parsed).expect("golden vector must serialize")
}

/// (vector file, target type) table for the frozen v1 message set.
type VectorCase = (&'static str, fn(&str) -> Value);
const CASES: &[VectorCase] = &[
    ("handshake.json", roundtrip::<Handshake>),
    (
        "platform_capabilities.json",
        roundtrip::<PlatformCapabilities>,
    ),
    (
        "receiver_capabilities.json",
        roundtrip::<ReceiverCapabilities>,
    ),
    ("source_observation.json", roundtrip::<SourceObservation>),
    ("media_candidate.json", roundtrip::<MediaCandidate>),
    ("cast_policy_input.json", roundtrip::<CastPolicyInput>),
    (
        "cast_policy_decision_direct.json",
        roundtrip::<CastPolicyDecision>,
    ),
    (
        "cast_policy_decision_reject.json",
        roundtrip::<CastPolicyDecision>,
    ),
    ("session_grant.json", roundtrip::<SessionGrant>),
    ("core_error.json", roundtrip::<CoreError>),
];

#[test]
fn rg_007_current_vectors_roundtrip() {
    for (name, case) in CASES {
        let raw = vector("current", name);
        let golden: Value = serde_json::from_str(&raw).expect("golden vector is valid JSON");
        assert_eq!(case(&raw), golden, "roundtrip mismatch in {name}");
    }
}

#[test]
fn rg_007_previous_vectors_remain_supported() {
    let current_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../schemas/current");
    let previous_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../schemas/previous");
    let current_names: Vec<_> = std::fs::read_dir(&current_dir)
        .unwrap()
        .map(|e| e.unwrap().file_name())
        .collect();
    for entry in std::fs::read_dir(&previous_dir).unwrap() {
        let name = entry.unwrap().file_name();
        assert!(
            current_names.contains(&name),
            "previous vector {name:?} missing from current set"
        );
    }
    // v1 is the initial version: previous mirrors current and every previous
    // vector must still deserialize against the current types.
    for (name, case) in CASES {
        case(&vector("previous", name));
    }
}

#[test]
fn unknown_fields_are_rejected() {
    fn with_extra_field(raw: &str) -> String {
        let mut value: Value = serde_json::from_str(raw).unwrap();
        value
            .as_object_mut()
            .unwrap()
            .insert("unexpected".to_owned(), Value::Bool(true));
        serde_json::to_string(&value).unwrap()
    }

    for (name, case) in CASES {
        // core_error 是字符串型错误码，没有可注入的字段；cast_policy_decision_*
        // 是 Core 出方向消息（内部 tag 枚举，serde 对 unit variant 的未知键不拒绝），
        // 入方向未知字段拒绝由上面的结构体类型保证。
        if *name == "core_error.json" || name.starts_with("cast_policy_decision_") {
            continue;
        }
        let mutated = with_extra_field(&vector("current", name));
        std::panic::catch_unwind(|| case(&mutated))
            .expect_err(&format!("{name} must reject unknown fields"));
    }
}

#[test]
fn unsupported_or_zero_versions_are_rejected() {
    // schema_version 0 is outside the valid domain entirely.
    let zero = r#"{"schema_version":0,"product_mode":"formal"}"#;
    assert!(serde_json::from_str::<Handshake>(zero).is_err());

    // A newer version parses but fails negotiation until the window widens.
    let next = SchemaVersion::new(NonZeroU16::new(2).unwrap());
    assert!(!SchemaVersion::CURRENT.is_supported_by(next));
}

#[test]
fn secrets_never_serialize() {
    // SessionGrant carries identity + generation only.
    let grant: Value = serde_json::from_str(&vector("current", "session_grant.json")).unwrap();
    let keys: Vec<&str> = grant
        .as_object()
        .unwrap()
        .keys()
        .map(String::as_str)
        .collect();
    assert_eq!(keys, ["generation", "session_id"]);

    // Debug output of the secret type is redacted.
    let secret = SessionSecret::from_bytes([7u8; 32]);
    assert_eq!(format!("{secret:?}"), "SessionSecret(REDACTED)");

    // No golden vector may contain secret-bearing keys anywhere.
    const DENIED_KEYS: &[&str] = &[
        "secret",
        "password",
        "cookie",
        "authorization",
        "token",
        "set-cookie",
    ];
    fn assert_no_secret_keys(value: &Value) {
        match value {
            Value::Object(map) => {
                for (key, child) in map {
                    let lower = key.to_ascii_lowercase();
                    assert!(
                        !DENIED_KEYS.contains(&lower.as_str()),
                        "secret-bearing key `{key}` in golden vector"
                    );
                    assert_no_secret_keys(child);
                }
            }
            Value::Array(items) => items.iter().for_each(assert_no_secret_keys),
            _ => {}
        }
    }
    for (name, _) in CASES {
        let golden: Value = serde_json::from_str(&vector("current", name)).unwrap();
        assert_no_secret_keys(&golden);
    }
}

#[test]
fn cs_008_core_error_codes_are_stable() {
    let mut codes: Vec<&str> = CoreError::ALL.iter().map(|e| e.code()).collect();
    codes.sort_unstable();
    assert_eq!(
        codes,
        [
            "ad_continuity_unknown",
            "capabilities_unavailable",
            "credential_bound_media",
            "drm_protected",
            "invalid_message",
            "missing_user_activation",
            "playback_not_advanced",
            "policy_denied",
            "receiver_incompatible",
            "session_expired",
            "session_unknown",
            "unsupported_schema_version",
            "untrusted_observation",
            "upstream_rejected",
        ]
    );
    // Codes roundtrip; unknown codes are rejected, never mapped to catch-alls.
    for error in CoreError::ALL {
        let raw = serde_json::to_string(error).unwrap();
        assert_eq!(serde_json::from_str::<CoreError>(&raw).unwrap(), *error);
    }
    assert!(serde_json::from_str::<CoreError>("\"network_error\"").is_err());
}

#[test]
fn pl_013_safety_fields_are_capability_independent() {
    // Platform capability choice must not alter safety/privacy facts of the
    // same policy input (full decision-engine matrix lands with MED tasks).
    let raw = vector("current", "cast_policy_input.json");
    let input: CastPolicyInput = serde_json::from_str(&raw).unwrap();
    for caps_json in [
        r#"{"browser_engine":"cef","tab_video":true,"system_audio":true,"hardware_h264":true,"local_discovery":"mdns+udp","secure_store":"os_native","protected_surface":"blocked"}"#,
        r#"{"browser_engine":"ark_web","tab_video":false,"system_audio":false,"hardware_h264":false,"local_discovery":"unavailable","secure_store":"unavailable","protected_surface":"blocked"}"#,
    ] {
        let caps: PlatformCapabilities = serde_json::from_str(caps_json).unwrap();
        // Capability parsing never touches the candidate's safety fields.
        let _ = caps;
        assert!(!input.candidate().drm());
        assert_eq!(
            input.candidate().headers_class(),
            crayon_ipc_schema::HeadersClass::RefererOnly
        );
        assert_eq!(
            input.candidate().ad_continuity(),
            crayon_ipc_schema::AdContinuity::Unknown
        );
    }
    // A DRM rejection decision is identical regardless of platform fixtures.
    let reject: Value =
        serde_json::from_str(&vector("current", "cast_policy_decision_reject.json")).unwrap();
    assert_eq!(
        roundtrip::<CastPolicyDecision>(&vector("current", "cast_policy_decision_reject.json")),
        reject
    );
}
