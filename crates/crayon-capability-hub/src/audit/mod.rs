//! Desensitized partner audit ledger (HUB-15).
//!
//! Audit dimensions are 64-bit hashes supplied by the caller — the ledger
//! never sees provider or tenant plaintext. Every emitted diagnostic is a
//! closed `DataClass::Diagnostic` event whose attributes carry only hash
//! hex and counters (HB-015: no body, no token, no full parameters).

use std::collections::BTreeMap;

use crayon_domain::{DataClass, DiagnosticEvent};
use crayon_partner_connector::api::ConnectorAuditEvent;

/// Maximum tracked dimensions; eviction is oldest-first with a dropped
/// counter (AGT-11 receipt-store pattern).
pub const MAX_AUDIT_DIMENSIONS: usize = 128;

/// One audit dimension: hashed identity plus closed policy facts.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct AuditDimension {
    /// Hash of the provider identity (caller-supplied; plaintext never
    /// enters this crate).
    pub provider_hash: u64,
    /// Hash of the tenant identity.
    pub tenant_hash: u64,
    /// Capability index (closed vocabulary owned by the caller).
    pub capability: u16,
    /// Route index (closed vocabulary owned by the caller).
    pub route: u16,
    /// Outcome index (closed vocabulary owned by the caller).
    pub outcome: u16,
}

/// Aggregated counters for one dimension.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct AuditCounters {
    pub calls: u64,
    pub failures: u64,
    /// Cumulative payload bytes, saturated at u64.
    pub payload_bytes: u64,
}

/// Failure of ledger operations; content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AuditError {
    /// The ledger is at capacity and the dimension is new.
    CapacityExceeded,
}

/// Bounded desensitized audit ledger.
#[derive(Default)]
pub struct AuditLedger {
    /// Insertion order for oldest-first eviction.
    order: Vec<AuditDimension>,
    counters: BTreeMap<AuditDimension, AuditCounters>,
    dropped: u64,
}

impl AuditLedger {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Records one observed call for a dimension. Payload bytes are
    /// saturating-added (only the bound crosses this boundary).
    pub fn record(&mut self, dimension: AuditDimension, failed: bool, payload_bytes: u64) {
        if self.counters.len() >= MAX_AUDIT_DIMENSIONS && !self.counters.contains_key(&dimension) {
            self.dropped += 1;
            // Evict the oldest tracked dimension to bound memory.
            if let Some(oldest) = self.order.first().copied() {
                self.counters.remove(&oldest);
                self.order.remove(0);
            } else {
                return;
            }
        }
        let entry = self.counters.entry(dimension).or_insert_with(|| {
            self.order.push(dimension);
            AuditCounters::default()
        });
        entry.calls = entry.calls.saturating_add(1);
        if failed {
            entry.failures = entry.failures.saturating_add(1);
        }
        entry.payload_bytes = entry.payload_bytes.saturating_add(payload_bytes);
    }

    /// Records a partner connector audit event. The event already carries
    /// desensitized facts; `provider_hash`/`tenant_hash` come from the
    /// caller's hashing, `route` from the caller's closed route vocabulary.
    /// Capability stays 0: the partner surface has no inbound capability
    /// index (dimensions are caller-owned closed vocabularies).
    pub fn record_connector(
        &mut self,
        event: &ConnectorAuditEvent,
        provider_hash: u64,
        tenant_hash: u64,
        route: u16,
    ) {
        let failed = event.outcome == crayon_partner_connector::api::ConnectorOutcome::Failed;
        let dimension = AuditDimension {
            provider_hash,
            tenant_hash,
            capability: 0,
            route,
            outcome: u16::from(!failed),
        };
        self.record(dimension, failed, event.payload_bytes as u64);
    }

    /// Counters for one dimension.
    #[must_use]
    pub fn counters(&self, dimension: &AuditDimension) -> Option<AuditCounters> {
        self.counters.get(dimension).copied()
    }

    /// Evicted-plus-shed count of untrackable dimensions.
    #[must_use]
    pub const fn dropped(&self) -> u64 {
        self.dropped
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.counters.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.counters.is_empty()
    }

    /// Emits a closed diagnostic event for one dimension. Attributes are
    /// hash hex and counters only — never provider/tenant plaintext.
    #[must_use]
    pub fn to_diagnostic(
        &self,
        dimension: &AuditDimension,
        timestamp_ms: u64,
    ) -> Option<DiagnosticEvent> {
        let counters = self.counters.get(dimension)?;
        let event = DiagnosticEvent::new(DataClass::Diagnostic, "hub.partner.audit", timestamp_ms)
            .ok()?
            .with_attribute(
                "provider_hash",
                &format!("{:016x}", dimension.provider_hash),
            )
            .ok()?
            .with_attribute("tenant_hash", &format!("{:016x}", dimension.tenant_hash))
            .ok()?
            .with_attribute("capability", &dimension.capability.to_string())
            .ok()?
            .with_attribute("route", &dimension.route.to_string())
            .ok()?
            .with_attribute("outcome", &dimension.outcome.to_string())
            .ok()?
            .with_attribute("calls", &counters.calls.to_string())
            .ok()?
            .with_attribute("failures", &counters.failures.to_string())
            .ok()?
            .with_attribute("payload_bytes", &counters.payload_bytes.to_string())
            .ok()?;
        Some(event)
    }
}
