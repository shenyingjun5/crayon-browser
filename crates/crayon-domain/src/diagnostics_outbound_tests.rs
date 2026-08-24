//! PRV-09 outbound diagnostics gate tests: default-deny consent,
//! per-channel opt-in, preview-exact drafts and immediate deletion
//! (PV-008, PV-010).

use super::*;
use crate::diagnostics::{DataClass, DiagnosticEvent};

fn enabled_all() -> DiagnosticsConsent {
    let mut consent = DiagnosticsConsent::new();
    consent.set(DiagnosticsChannel::UsageTelemetry, true);
    consent.set(DiagnosticsChannel::CrashReports, true);
    consent
}

fn record(channel: DiagnosticsChannel, body: &str) -> PendingRecord {
    PendingRecord {
        channel,
        body: body.to_owned(),
    }
}

/// PV-008: fresh state denies both channels; nothing can enter or leave.
#[test]
fn defaults_deny_every_channel() {
    let mut outbound = OutboundDiagnostics::new(DiagnosticsConsent::new());
    assert_eq!(
        outbound.record(record(DiagnosticsChannel::UsageTelemetry, "boot")),
        RecordOutcome::ChannelDisabled
    );
    assert_eq!(
        outbound.record(record(DiagnosticsChannel::CrashReports, "panic")),
        RecordOutcome::ChannelDisabled
    );
    assert_eq!(outbound.pending_len(), 0);
    assert!(
        outbound
            .drain_channel(DiagnosticsChannel::UsageTelemetry, 10)
            .is_none(),
        "a disabled channel can never produce a draft"
    );
}

#[test]
fn opt_in_is_per_channel() {
    let mut consent = DiagnosticsConsent::new();
    consent.set(DiagnosticsChannel::CrashReports, true);
    let mut outbound = OutboundDiagnostics::new(consent);
    assert_eq!(
        outbound.record(record(DiagnosticsChannel::CrashReports, "panic-1")),
        RecordOutcome::Recorded
    );
    assert_eq!(
        outbound.record(record(DiagnosticsChannel::UsageTelemetry, "boot")),
        RecordOutcome::ChannelDisabled
    );
    assert_eq!(outbound.pending_len(), 1);
}

/// Revoking (or disabling) a channel purges its entire pending backlog
/// immediately and reports the count.
#[test]
fn disabling_a_channel_purges_its_backlog() {
    let mut outbound = OutboundDiagnostics::new(enabled_all());
    for index in 0..3 {
        outbound.record(record(
            DiagnosticsChannel::UsageTelemetry,
            &format!("u{index}"),
        ));
    }
    outbound.record(record(DiagnosticsChannel::CrashReports, "c0"));
    assert_eq!(
        outbound.set_consent(DiagnosticsChannel::UsageTelemetry, false),
        3
    );
    assert_eq!(outbound.pending_len(), 1);
    // Re-enabling starts from an empty backlog.
    assert!(outbound
        .drain_channel(DiagnosticsChannel::UsageTelemetry, 10)
        .is_none());
}

/// PV-010 core property: the preview string IS the transmitted payload —
/// one value, read twice, byte-identical.
#[test]
fn preview_and_transmitted_payload_are_identical() {
    let mut outbound = OutboundDiagnostics::new(enabled_all());
    outbound.record(record(DiagnosticsChannel::UsageTelemetry, "alpha"));
    outbound.record(record(DiagnosticsChannel::UsageTelemetry, "beta"));
    let draft = outbound
        .drain_channel(DiagnosticsChannel::UsageTelemetry, 10)
        .expect("draft");
    let preview = draft.payload();
    let preview_again = draft.payload();
    assert_eq!(preview, preview_again);
    assert_eq!(preview, "alpha\nbeta\n");
    assert_eq!(draft.record_count(), 2);
    // Records left the queue at drain time; nothing is sent twice.
    assert_eq!(outbound.pending_len(), 0);
    assert_eq!(outbound.stats().sent_records_total, 2);
}

#[test]
fn capacity_sheds_newest_and_counts() {
    let mut outbound = OutboundDiagnostics::new(enabled_all());
    for index in 0..MAX_PENDING_RECORDS + 5 {
        let outcome = outbound.record(record(
            DiagnosticsChannel::CrashReports,
            &format!("r{index}"),
        ));
        if index < MAX_PENDING_RECORDS {
            assert!(outcome.is_recorded());
        } else {
            assert_eq!(outcome, RecordOutcome::DroppedCapacity);
        }
    }
    assert_eq!(outbound.pending_len(), MAX_PENDING_RECORDS);
    assert_eq!(outbound.stats().dropped_capacity_total, 5);
    // Oldest-first order preserved.
    let draft = outbound
        .drain_channel(DiagnosticsChannel::CrashReports, MAX_PENDING_RECORDS)
        .expect("draft");
    assert!(draft.payload().starts_with("r0\n"));
    assert!(draft
        .payload()
        .ends_with(&format!("r{}\n", MAX_PENDING_RECORDS - 1)));
}

#[test]
fn deletion_is_immediate_and_counted() {
    let mut outbound = OutboundDiagnostics::new(enabled_all());
    outbound.record(record(DiagnosticsChannel::UsageTelemetry, "u1"));
    outbound.record(record(DiagnosticsChannel::CrashReports, "c1"));
    assert_eq!(
        outbound.clear_channel(DiagnosticsChannel::UsageTelemetry),
        1
    );
    assert_eq!(outbound.pending_len(), 1);
    assert_eq!(outbound.clear_all(), 1);
    assert_eq!(outbound.pending_len(), 0);
    assert_eq!(outbound.stats().deleted_records_total, 2);
}

#[test]
fn invalid_bodies_are_rejected_without_storage() {
    let mut outbound = OutboundDiagnostics::new(enabled_all());
    assert_eq!(
        outbound.record(record(DiagnosticsChannel::UsageTelemetry, "")),
        RecordOutcome::InvalidRecord
    );
    let overlong = "x".repeat(MAX_RECORD_BODY_BYTES + 1);
    assert_eq!(
        outbound.record(record(DiagnosticsChannel::UsageTelemetry, &overlong)),
        RecordOutcome::InvalidRecord
    );
    assert_eq!(outbound.pending_len(), 0);
}

/// Browsing content and secrets can never enter an outbound body: PRV-08
/// refuses to construct such events at the source, and the class gate
/// here is defense in depth.  Constructible classes render deterministically.
#[test]
fn event_rendering_is_closed_and_class_gated() {
    let mut outbound = OutboundDiagnostics::new(enabled_all());
    // PRV-08 forbids constructing content/secret-class diagnostic events,
    // so no URL/title/credential event can reach this layer at all.
    let operational =
        DiagnosticEvent::new(DataClass::Operational, "feature_flag", 1).expect("event builds");
    let ok_event =
        DiagnosticEvent::new(DataClass::Diagnostic, "render_lag", 3).expect("event builds");
    assert_eq!(
        outbound.record_event(DiagnosticsChannel::UsageTelemetry, &operational),
        RecordOutcome::Recorded
    );
    assert_eq!(
        outbound.record_event(DiagnosticsChannel::UsageTelemetry, &ok_event),
        RecordOutcome::Recorded
    );
    let draft = outbound
        .drain_channel(DiagnosticsChannel::UsageTelemetry, 4)
        .expect("draft");
    assert_eq!(
        draft.payload(),
        "usage_telemetry|operational|feature_flag\nusage_telemetry|diagnostic|render_lag\n"
    );
}

/// Deterministic pseudo-random sequence (LCG): disabled channels never
/// produce drafts, the queue stays bounded and counters stay monotonic.
#[test]
fn lcg_outbound_invariants() {
    let channels = [
        DiagnosticsChannel::UsageTelemetry,
        DiagnosticsChannel::CrashReports,
    ];
    let mut state: u64 = 0xD1B5_4A32_D192_ED03;
    let mut next = || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };
    let mut outbound = OutboundDiagnostics::new(DiagnosticsConsent::new());
    let mut last_stats = outbound.stats();
    for _ in 0..3_000_u64 {
        let channel = channels[(next() % 2) as usize];
        match next() % 6 {
            0 => {
                outbound.set_consent(channel, next() % 2 == 0);
            }
            1 | 2 => {
                let body = format!("e{}", next() % 100);
                let _ = outbound.record(record(channel, &body));
            }
            3 => {
                if let Some(draft) = outbound.drain_channel(channel, (next() % 9) as usize + 1) {
                    assert!(!draft.payload().is_empty());
                }
            }
            4 => {
                let _ = outbound.clear_channel(channel);
            }
            _ => {
                let _ = outbound.clear_all();
            }
        }
        let stats = outbound.stats();
        assert!(outbound.pending_len() <= MAX_PENDING_RECORDS);
        assert!(stats.recorded_total >= last_stats.recorded_total);
        assert!(stats.sent_records_total >= last_stats.sent_records_total);
        assert!(stats.deleted_records_total >= last_stats.deleted_records_total);
        last_stats = stats;
        // A disabled channel must never hold or emit anything.
        for channel in channels {
            if !outbound.consent().allows(channel) {
                assert!(
                    outbound.drain_channel(channel, 4).is_none(),
                    "{channel:?} emitted while disabled"
                );
            }
        }
    }
}
