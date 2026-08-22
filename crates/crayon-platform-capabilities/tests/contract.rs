//! Contract tests for the platform adapter capability model (PLT-02):
//! golden roundtrip, previous-window mirroring, unknown-field/version
//! rejection, consistency folding and the two expected platform profiles.

use crayon_platform_capabilities::{
    AgentIpcTransport, CapabilityError, PlatformAdapterCapabilities, SecureStoreBackend,
    SupportLevel,
};
use serde_json::Value;
use std::path::PathBuf;

fn vector(set: &str, name: &str) -> String {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../schemas")
        .join(set)
        .join(name);
    std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {}: {e}", path.display()))
}

const VECTORS: &[&str] = &[
    "platform_adapter_capabilities.json",
    "platform_adapter_capabilities_windows.json",
    "platform_adapter_capabilities_macos.json",
];

#[test]
fn golden_vectors_roundtrip() {
    for name in VECTORS {
        let raw = vector("current", name);
        let parsed: PlatformAdapterCapabilities =
            serde_json::from_str(&raw).expect("golden vector must deserialize");
        parsed.validate().expect("golden vector must validate");
        let serialized = serde_json::to_value(parsed).expect("serialize");
        let golden: Value = serde_json::from_str(&raw).expect("golden vector is valid JSON");
        assert_eq!(serialized, golden, "roundtrip mismatch in {name}");
    }
}

#[test]
fn previous_vectors_mirror_current() {
    // v1 is the initial version: previous mirrors current byte-for-byte.
    for name in VECTORS {
        assert_eq!(vector("current", name), vector("previous", name), "{name}");
        let parsed: PlatformAdapterCapabilities =
            serde_json::from_str(&vector("previous", name)).expect("previous must decode");
        parsed.validate().expect("previous must validate");
    }
}

#[test]
fn unknown_fields_and_wrong_schema_are_rejected() {
    let raw = vector("current", "platform_adapter_capabilities.json");
    let with_extra = raw.replace("\"schema\":1", "\"schema\":1,\"extra\":true");
    assert!(serde_json::from_str::<PlatformAdapterCapabilities>(&with_extra).is_err());

    let wrong_version = raw.replace("\"schema\":1", "\"schema\":2");
    let decoded: PlatformAdapterCapabilities =
        serde_json::from_str(&wrong_version).expect("decode");
    assert_eq!(decoded.validate(), Err(CapabilityError::UnsupportedSchema));
}

#[test]
fn inconsistent_transport_combination_fails_closed() {
    // Peer credentials without a transport are contradictory.
    let raw = vector("current", "platform_adapter_capabilities.json").replace(
        "\"transport\":\"named_pipe\"",
        "\"transport\":\"unavailable\"",
    );
    let decoded: PlatformAdapterCapabilities = serde_json::from_str(&raw).expect("decode");
    assert_eq!(decoded.validate(), Err(CapabilityError::Inconsistent));
    // Normalize folds the stray flags off.
    let normalized = decoded.normalized();
    assert!(!normalized.local_agent_ipc.peer_credentials);
    assert!(!normalized.local_agent_ipc.per_user_acl);
    assert!(normalized.validate().is_ok());
}

#[test]
fn expected_platform_profiles_are_anchored() {
    // CP-W01 profile: DPAPI + named pipe + observation available.
    let windows: PlatformAdapterCapabilities = serde_json::from_str(&vector(
        "current",
        "platform_adapter_capabilities_windows.json",
    ))
    .expect("windows profile");
    assert_eq!(windows.secure_store.backend, SecureStoreBackend::Dpapi);
    assert_eq!(
        windows.local_agent_ipc.transport,
        AgentIpcTransport::NamedPipe
    );
    assert_eq!(windows.local_network.observation, SupportLevel::Available);

    // CP-M01 profile: Keychain + UDS + local network requires permission.
    let macos: PlatformAdapterCapabilities = serde_json::from_str(&vector(
        "current",
        "platform_adapter_capabilities_macos.json",
    ))
    .expect("macos profile");
    assert_eq!(macos.secure_store.backend, SecureStoreBackend::Keychain);
    assert_eq!(
        macos.local_agent_ipc.transport,
        AgentIpcTransport::UnixDomainSocket
    );
    assert_eq!(
        macos.local_network.observation,
        SupportLevel::RequiresPermission
    );
}

#[test]
fn enum_sets_are_closed() {
    for forbidden in [
        "\"partially_available\"",
        "\"deprecated\"",
        "\"loopback_only\"",
    ] {
        assert!(serde_json::from_str::<SupportLevel>(forbidden).is_err());
    }
    for forbidden in ["\"dpapi2\"", "\"file_based\"", "\"plain_text\""] {
        assert!(serde_json::from_str::<SecureStoreBackend>(forbidden).is_err());
    }
    for forbidden in ["\"tcp\"", "\"websocket\"", "\"remote_pipe\""] {
        assert!(serde_json::from_str::<AgentIpcTransport>(forbidden).is_err());
    }
    // A usable surface is exactly "available".
    assert!(SupportLevel::Available.is_usable());
    assert!(!SupportLevel::RequiresPermission.is_usable());
    assert!(!SupportLevel::Unavailable.is_usable());
}
