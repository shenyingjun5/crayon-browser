//! HB-015 coverage: hashed dimensions, bounded LRU ledger with dropped
//! counter, closed diagnostic emission and partner event conversion. No
//! provider/tenant plaintext ever enters or leaves the ledger.

use crate::audit::MAX_AUDIT_DIMENSIONS;
use crate::audit::{AuditDimension, AuditError, AuditLedger};
use crayon_domain::DataClass;
use crayon_partner_connector::api::{
    ConnectorAuditEvent, ConnectorCall, ConnectorId, ConnectorOutcome,
};

fn dimension(provider: u64) -> AuditDimension {
    AuditDimension {
        provider_hash: provider,
        tenant_hash: 42,
        capability: 3,
        route: 1,
        outcome: 1,
    }
}

fn partner_event(body: &str) -> ConnectorAuditEvent {
    let id = ConnectorId::new("acme.notes").expect("id");
    let call = ConnectorCall::new("notes-sync", body).expect("call");
    ConnectorAuditEvent::from_call(&id, &call, ConnectorOutcome::Completed).expect("event")
}

#[test]
fn dimensions_aggregate_counters() {
    let mut ledger = AuditLedger::new();
    let dim = dimension(1);
    ledger.record(dim, false, 100);
    ledger.record(dim, false, 50);
    let counters = ledger.counters(&dim).expect("counters");
    assert_eq!(counters.calls, 2);
    assert_eq!(counters.failures, 0);
    assert_eq!(counters.payload_bytes, 150);
    assert_eq!(ledger.len(), 1);
    assert!(!ledger.is_empty());
}

#[test]
fn failures_are_counted_separately() {
    let mut ledger = AuditLedger::new();
    let dim = dimension(7);
    ledger.record(dim, false, 10);
    ledger.record(dim, true, 20);
    let counters = ledger.counters(&dim).expect("counters");
    assert_eq!(counters.calls, 2);
    assert_eq!(counters.failures, 1);
}

#[test]
fn capacity_eviction_is_oldest_first_with_dropped_counter() {
    let mut ledger = AuditLedger::new();
    for provider in 0..MAX_AUDIT_DIMENSIONS as u64 {
        ledger.record(dimension(provider), false, 1);
    }
    assert_eq!(ledger.len(), MAX_AUDIT_DIMENSIONS);
    assert_eq!(ledger.dropped(), 0);
    // A new dimension evicts the oldest and counts the loss.
    ledger.record(dimension(9_999), false, 1);
    assert_eq!(ledger.len(), MAX_AUDIT_DIMENSIONS);
    assert_eq!(ledger.dropped(), 1);
    assert!(ledger.counters(&dimension(0)).is_none());
    assert!(ledger.counters(&dimension(9_999)).is_some());
}

#[test]
fn diagnostic_emission_carries_only_hashes_and_counters() {
    let mut ledger = AuditLedger::new();
    let dim = dimension(0xdead_beef_cafe_f00d);
    ledger.record(dim, false, 77);
    let event = ledger.to_diagnostic(&dim, 1_234).expect("diagnostic");
    assert_eq!(event.class(), DataClass::Diagnostic);
    let encoded = format!("{event:?}");
    assert!(encoded.contains("deadbeefcafef00d"));
    assert!(encoded.contains("hub.partner.audit"));
    // No plaintext: the event is hash-and-counter only.
    assert!(!encoded.contains("https://"));
}

#[test]
fn partner_events_convert_into_ledger_dimensions() {
    let mut ledger = AuditLedger::new();
    let event = partner_event("{\"note\":\"body\"}");
    ledger.record_connector(&event, 0xaaaa, 0xbbbb, 2);
    let dim = AuditDimension {
        provider_hash: 0xaaaa,
        tenant_hash: 0xbbbb,
        capability: 0,
        route: 2,
        outcome: 1, // not failed
    };
    let counters = ledger.counters(&dim).expect("counters");
    assert_eq!(counters.calls, 1);
    assert_eq!(counters.failures, 0);
    assert_eq!(counters.payload_bytes, event.payload_bytes as u64);
}

#[test]
fn empty_ledger_has_no_diagnostics() {
    let ledger = AuditLedger::new();
    assert!(ledger.to_diagnostic(&dimension(1), 0).is_none());
    assert!(ledger.is_empty());
}

#[test]
fn audit_error_is_content_free() {
    // CapacityExceeded carries no dimension or payload data.
    let error = AuditError::CapacityExceeded;
    assert_eq!(error, AuditError::CapacityExceeded);
}
