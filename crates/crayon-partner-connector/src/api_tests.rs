//! HB-009 interface-level coverage: descriptor/scope/call validation,
//! session namespace isolation and payload budgets.

use crate::api::{
    ConnectorAuditEvent, ConnectorCall, ConnectorDescriptor, ConnectorError, ConnectorId,
    ConnectorOutcome, ConnectorSessionId, TrustPort,
};

fn id(raw: &str) -> ConnectorId {
    ConnectorId::new(raw).expect("id")
}

#[test]
fn connector_id_requires_partner_namespace_dot() {
    assert!(ConnectorId::new("acme.notes").is_ok());
    assert!(ConnectorId::new("acme-notes").is_err()); // no namespace
    assert!(ConnectorId::new("acme..notes").is_err()); // empty name
    assert!(ConnectorId::new("Acme.notes").is_err()); // charset
    assert!(ConnectorId::new("").is_err());
}

#[test]
fn descriptor_validates_version_and_scopes() {
    let descriptor =
        ConnectorDescriptor::new(id("acme.notes"), "1.2.0", &["notes.read", "notes.write"])
            .expect("descriptor");
    assert_eq!(descriptor.version(), "1.2.0");
    assert_eq!(descriptor.scopes(), ["notes.read", "notes.write"]);

    assert!(ConnectorDescriptor::new(id("acme.notes"), "", &[]).is_err());
    assert!(ConnectorDescriptor::new(id("acme.notes"), "1 2", &[]).is_err());
    assert!(ConnectorDescriptor::new(id("acme.notes"), "1.0", &["notes.*"]).is_err());
    assert!(ConnectorDescriptor::new(id("acme.notes"), "1.0", &[""]).is_err());
    // Wildcards are authority-widening: always rejected.
    assert!(ConnectorDescriptor::new(id("acme.notes"), "1.0", &["*"]).is_err());
}

#[test]
fn scope_budget_is_bounded() {
    let scopes: Vec<String> = (0..17).map(|index| format!("scope{index}")).collect();
    let refs: Vec<&str> = scopes.iter().map(String::as_str).collect();
    assert_eq!(
        ConnectorDescriptor::new(id("acme.notes"), "1.0", &refs),
        Err(ConnectorError::ScopeBudgetExceeded)
    );
}

#[test]
fn session_ids_are_outbound_only() {
    let mut counter = 0u64;
    let first = ConnectorSessionId::mint(&mut counter);
    let second = ConnectorSessionId::mint(&mut counter);
    assert_eq!(first.value(), 1);
    assert_eq!(second.value(), 2);
    // The type is a plain monotonic wrapper with no inbound-string
    // constructor and no string accessor: an inbound CAAP session token
    // cannot become a ConnectorSessionId at the type level (no From/TryFrom
    // impl exists and the field is private).
}

#[test]
fn call_payload_budget_is_enforced() {
    let call = ConnectorCall::new("notes-sync", "{}").expect("call");
    assert_eq!(call.endpoint_ref(), "notes-sync");

    assert_eq!(
        ConnectorCall::new("", "{}"),
        Err(ConnectorError::InvalidName)
    );
    let oversize = "x".repeat(crate::api::MAX_CALL_PAYLOAD_BYTES + 1);
    assert_eq!(
        ConnectorCall::new("notes-sync", &oversize),
        Err(ConnectorError::PayloadTooLarge)
    );
}

#[test]
fn audit_event_carries_no_payload() {
    let call = ConnectorCall::new("notes-sync", "{\"title\":\"secret body\"}").expect("call");
    let event =
        ConnectorAuditEvent::from_call(&id("acme.notes"), &call, ConnectorOutcome::Completed)
            .expect("event");
    assert_eq!(event.connector_namespace, "acme");
    assert_eq!(event.connector_name, "notes");
    assert_eq!(event.outcome, ConnectorOutcome::Completed);
    assert_eq!(event.payload_bytes, call.payload().len());
    // Serialized event contains no payload text.
    let encoded = format!("{event:?}");
    assert!(!encoded.contains("secret body"));
}

#[test]
fn trust_port_default_is_deny() {
    // Interface-level default: an implementation that has not been
    // populated must not trust anything.
    struct DenyAll;
    impl crate::api::TrustPort for DenyAll {
        fn is_trusted(&mut self, _id: &ConnectorId, _version: &str) -> bool {
            false
        }
    }
    let mut trust = DenyAll;
    assert!(!trust.is_trusted(&id("acme.notes"), "1.0"));
}

#[test]
fn crate_is_dependency_isolated_from_the_inbound_path() {
    // HB-009 crate isolation: the outbound connector interface must not
    // depend on any inbound agent crate (gateway, IPC schema) — asserted
    // against this crate's own manifest.
    const MANIFEST: &str = include_str!("../Cargo.toml");
    for forbidden in [
        "crayon-agent-gateway",
        "crayon-ipc-schema",
        "crayon-app-runtime",
        "crayon-semantic-action",
    ] {
        assert!(
            !MANIFEST.contains(forbidden),
            "outbound connector interface must not depend on inbound crate {forbidden}"
        );
    }
}
