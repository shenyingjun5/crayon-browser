//! Contract tests for the macOS adapter capability document (M04a).

use super::*;
use crayon_platform_capabilities::SupportLevel;

#[test]
fn document_is_schema_valid_and_truthful_for_m04d() {
    let doc = super::macos_adapter_capabilities();
    doc.validate().expect("schema valid");
    assert_eq!(doc.schema(), 1);
    // M04a truth: only Keychain secure storage is delivered.  Rotation
    // is supported (delete + re-add under the same key).
    assert_eq!(
        doc.secure_store.backend,
        crayon_platform_capabilities::SecureStoreBackend::Keychain
    );
    assert!(doc.secure_store.rotation);
    // M04b delivered local network and lifecycle.
    assert_eq!(
        doc.local_network.observation,
        crayon_platform_capabilities::SupportLevel::RequiresPermission
    );
    assert!(doc.local_network.change_events);
    assert!(doc.lifecycle.power_events && doc.lifecycle.lock_events);

    assert_eq!(doc.update.service, SupportLevel::Available);
    assert_eq!(
        doc.local_agent_ipc.transport,
        crayon_platform_capabilities::AgentIpcTransport::UnixDomainSocket
    );
    assert!(doc.local_agent_ipc.peer_credentials);
    assert!(doc.local_agent_ipc.per_user_acl);
    assert!(doc.external_client_handoff.download);
    assert!(doc.external_client_handoff.launch);
}

#[test]
fn document_wire_roundtrip() {
    let doc = super::macos_adapter_capabilities();
    let json = serde_json::to_string(&doc).expect("serialize");
    let parsed: PlatformAdapterCapabilities = serde_json::from_str(&json).expect("deserialize");
    assert_eq!(parsed, doc);
}

#[test]
fn unknown_field_is_rejected() {
    let doc = super::macos_adapter_capabilities();
    let mut json = serde_json::to_value(doc).expect("value");
    json["bogus_field"] = serde_json::json!(1);
    let text = serde_json::to_string(&json).expect("text");
    let parsed: Result<PlatformAdapterCapabilities, _> = serde_json::from_str(&text);
    assert!(parsed.is_err(), "deny_unknown_fields must reject");
}
