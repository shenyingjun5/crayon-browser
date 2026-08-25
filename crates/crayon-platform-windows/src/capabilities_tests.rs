//! Contract tests for the Windows adapter capability document.

use super::*;
use crayon_platform_capabilities::{AgentIpcTransport, SupportLevel};

#[test]
fn document_is_schema_valid_and_truthful_for_w04_complete() {
    let doc = super::windows_adapter_capabilities();
    doc.validate().expect("schema valid");
    assert_eq!(doc.schema(), 1);
    // All four slices delivered; signed packages remain false until
    // QAR-09 defines the signing pipeline.
    assert_eq!(
        doc.secure_store.backend,
        crayon_platform_capabilities::SecureStoreBackend::Dpapi
    );
    assert!(!doc.secure_store.rotation);
    assert_eq!(doc.local_network.observation, SupportLevel::Available);
    assert!(doc.local_network.change_events);
    assert!(doc.lifecycle.power_events && doc.lifecycle.lock_events);
    assert_eq!(doc.update.service, SupportLevel::Available);
    assert_eq!(doc.local_agent_ipc.transport, AgentIpcTransport::NamedPipe);
    assert!(doc.local_agent_ipc.peer_credentials);
    assert!(doc.local_agent_ipc.per_user_acl);
    assert!(doc.external_client_handoff.download);
    assert!(doc.external_client_handoff.launch);
}

#[test]
fn document_wire_roundtrip_matches_windows_profile_shape() {
    let doc = super::windows_adapter_capabilities();
    let json = serde_json::to_string(&doc).expect("serialize");
    let parsed: PlatformAdapterCapabilities = serde_json::from_str(&json).expect("deserialize");
    assert_eq!(parsed, doc);
}

#[test]
fn unknown_field_is_rejected() {
    let doc = super::windows_adapter_capabilities();
    let mut json = serde_json::to_value(doc).expect("value");
    json["bogus_field"] = serde_json::json!(1);
    let text = serde_json::to_string(&json).expect("text");
    let parsed: Result<PlatformAdapterCapabilities, _> = serde_json::from_str(&text);
    assert!(parsed.is_err(), "deny_unknown_fields must reject");
}
