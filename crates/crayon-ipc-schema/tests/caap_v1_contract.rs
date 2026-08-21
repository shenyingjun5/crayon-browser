//! CAAP v1 envelope contract tests (AGT-01): golden roundtrip against
//! `schemas/current` and `schemas/previous`, unknown field / version /
//! bounds rejection and a deterministic pseudo-fuzz pass (AG-001).

use crayon_ipc_schema::{
    CaapCancel, CaapChunk, CaapErrorReply, CaapHello, CaapRequest, CaapSchemaError, CaapWelcome,
    SchemaVersion, MAX_CAAP_CAPABILITIES, MAX_CAAP_CHUNK_BYTES, MAX_CAAP_PARAMS,
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

type VectorCase = (&'static str, fn(&str) -> Value);
const CASES: &[VectorCase] = &[
    ("caap_hello.json", roundtrip::<CaapHello>),
    ("caap_welcome.json", roundtrip::<CaapWelcome>),
    ("caap_request.json", roundtrip::<CaapRequest>),
    ("caap_chunk.json", roundtrip::<CaapChunk>),
    ("caap_cancel.json", roundtrip::<CaapCancel>),
    ("caap_error_reply.json", roundtrip::<CaapErrorReply>),
];

#[test]
fn ag_001_current_vectors_roundtrip() {
    for (name, case) in CASES {
        let raw = vector("current", name);
        let golden: Value = serde_json::from_str(&raw).expect("golden vector is valid JSON");
        assert_eq!(case(&raw), golden, "roundtrip mismatch in {name}");
    }
}

#[test]
fn ag_001_previous_vectors_remain_supported() {
    // v1 is the initial version: previous mirrors current byte-for-byte
    // and every previous vector deserializes against the current types.
    for (name, case) in CASES {
        let current = vector("current", name);
        let previous = vector("previous", name);
        assert_eq!(current, previous, "v1 previous must mirror current: {name}");
        case(&previous);
    }
}

#[test]
fn ag_001_unknown_fields_and_zero_version_are_rejected() {
    let hello = vector("current", "caap_hello.json");
    let with_extra = hello.replace("\"schema\":1", "\"schema\":1,\"extra\":true");
    assert!(serde_json::from_str::<CaapHello>(&with_extra).is_err());
    let zero_version = hello.replace("\"schema\":1", "\"schema\":0");
    assert!(serde_json::from_str::<CaapHello>(&zero_version).is_err());
    // Every message type denies unknown fields.
    for (name, _) in CASES {
        let raw = vector("current", name);
        let value: Value = serde_json::from_str(&raw).unwrap();
        let mut object = value.as_object().unwrap().clone();
        object.insert("unexpected".to_owned(), Value::Null);
        let mutated = serde_json::to_string(&object).unwrap();
        assert!(
            serde_json::from_str::<CaapHello>(&mutated).is_err()
                && serde_json::from_str::<CaapWelcome>(&mutated).is_err()
                && serde_json::from_str::<CaapRequest>(&mutated).is_err()
                && serde_json::from_str::<CaapChunk>(&mutated).is_err()
                && serde_json::from_str::<CaapCancel>(&mutated).is_err()
                && serde_json::from_str::<CaapErrorReply>(&mutated).is_err(),
            "unknown field must be rejected for {name}"
        );
    }
}

#[test]
fn ag_001_bounds_are_enforced() {
    let version = SchemaVersion::new(NonZeroU16::MIN);
    // Token bounds.
    assert_eq!(
        CaapHello::new(version, &"c".repeat(65), vec![]),
        Err(CaapSchemaError::InvalidToken)
    );
    assert_eq!(
        CaapHello::new(version, "Bad Client", vec![]),
        Err(CaapSchemaError::InvalidToken)
    );
    // Capability bound.
    assert_eq!(
        CaapHello::new(
            version,
            "cli",
            vec![crayon_domain::AgentCapability::PageRead; MAX_CAAP_CAPABILITIES + 1]
        ),
        Err(CaapSchemaError::TooManyCapabilities)
    );
    // Parameter bounds.
    let mut params = std::collections::BTreeMap::new();
    for index in 0..=MAX_CAAP_PARAMS {
        params.insert(format!("k{index}"), "v".to_owned());
    }
    let target = crayon_domain::AgentTarget::ActiveTab;
    assert_eq!(
        CaapRequest::new(1, "tool", target.clone(), 0, "key", params),
        Err(CaapSchemaError::TooManyParams)
    );
    let mut long_value = std::collections::BTreeMap::new();
    long_value.insert("k".to_owned(), "v".repeat(1025));
    assert_eq!(
        CaapRequest::new(1, "tool", target, 0, "key", long_value),
        Err(CaapSchemaError::ParamValueTooLong)
    );
    // Chunk bound.
    assert_eq!(
        CaapChunk::new(1, 0, &"x".repeat(MAX_CAAP_CHUNK_BYTES + 1), false),
        Err(CaapSchemaError::ChunkTooLarge)
    );
    assert!(CaapChunk::new(1, 0, &"x".repeat(MAX_CAAP_CHUNK_BYTES), true).is_ok());
    // validate() re-checks decoded payloads: a mutated JSON document with
    // an overlong chunk parses but fails validation.
    let chunk = vector("current", "caap_chunk.json")
        .replace("# Example page", &"x".repeat(MAX_CAAP_CHUNK_BYTES + 1));
    let decoded: CaapChunk = serde_json::from_str(&chunk).expect("decode");
    assert_eq!(decoded.validate(), Err(CaapSchemaError::ChunkTooLarge));
}

/// Deterministic pseudo-fuzz: mutate and truncate the golden vectors with
/// a fixed-seed LCG; decoding must always terminate with Ok or Err and
/// never panic.
#[test]
fn ag_001_decoding_never_panics_on_mutated_input() {
    let mut state: u64 = 0x5EED_F00D_1234_5678;
    let mut next_byte = move || {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        (state >> 33) as u8
    };
    for (name, _) in CASES {
        let raw = vector("current", name).into_bytes();
        for _ in 0..200 {
            let mut mutated = raw.clone();
            // Flip one byte at a pseudo-random position.
            let position = (next_byte() as usize) % mutated.len();
            mutated[position] = next_byte();
            // Truncate at a pseudo-random length.
            let cut = (next_byte() as usize) % (mutated.len() + 1);
            mutated.truncate(cut);
            let text = String::from_utf8_lossy(&mutated);
            // All outcomes are acceptable except a panic.
            let _ = serde_json::from_str::<CaapHello>(&text);
            let _ = serde_json::from_str::<CaapWelcome>(&text);
            let _ = serde_json::from_str::<CaapRequest>(&text);
            let _ = serde_json::from_str::<CaapChunk>(&text);
            let _ = serde_json::from_str::<CaapCancel>(&text);
            let _ = serde_json::from_str::<CaapErrorReply>(&text);
        }
    }
}
