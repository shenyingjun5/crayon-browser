//! Outbound diagnostics gate (PRV-09): consent, bounded pending queue,
//! preview-exact send drafts and immediate deletion.
//!
//! Both channels are denied by default (PV-008); crash reports are a
//! separate opt-in.  A record can only leave the process through a
//! [`SendDraft`] obtained from [`OutboundDiagnostics::drain_channel`],
//! and `SendDraft::payload()` is the exact byte source for both the
//! user-visible preview and the transmission — the two cannot diverge
//! (PV-010).  Revoking a channel purges its unsent records immediately.
//!
//! Record bodies are pre-redacted by callers on top of the PRV-08 data
//! plane; this layer adds no interpretation and performs no IO.

use crate::diagnostics::{DataClass, DiagnosticEvent};
use std::collections::VecDeque;

/// Maximum records held per outbound queue.
pub const MAX_PENDING_RECORDS: usize = 256;

/// Maximum single-record body length in bytes.
pub const MAX_RECORD_BODY_BYTES: usize = 2048;

/// Closed diagnostics channels with independent consent.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DiagnosticsChannel {
    /// Product usage telemetry.
    UsageTelemetry,
    /// Crash reports.
    CrashReports,
}

impl DiagnosticsChannel {
    /// Stable wire name.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::UsageTelemetry => "usage_telemetry",
            Self::CrashReports => "crash_reports",
        }
    }
}

/// Consent state per channel.  Construction is default-deny for both
/// channels; enabling is an explicit user action.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DiagnosticsConsent {
    usage_telemetry: bool,
    crash_reports: bool,
}

impl Default for DiagnosticsConsent {
    fn default() -> Self {
        Self::new()
    }
}

impl DiagnosticsConsent {
    /// Default-deny for every channel (PV-008).
    #[must_use]
    pub const fn new() -> Self {
        Self {
            usage_telemetry: false,
            crash_reports: false,
        }
    }

    /// Explicitly enables or disables one channel.
    pub fn set(&mut self, channel: DiagnosticsChannel, enabled: bool) {
        match channel {
            DiagnosticsChannel::UsageTelemetry => self.usage_telemetry = enabled,
            DiagnosticsChannel::CrashReports => self.crash_reports = enabled,
        }
    }

    /// Reports whether the channel may leave the process at all.
    #[must_use]
    pub const fn allows(self, channel: DiagnosticsChannel) -> bool {
        match channel {
            DiagnosticsChannel::UsageTelemetry => self.usage_telemetry,
            DiagnosticsChannel::CrashReports => self.crash_reports,
        }
    }
}

/// One queued outbound record.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PendingRecord {
    pub channel: DiagnosticsChannel,
    pub body: String,
}

/// Record failure or shedding outcome.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RecordOutcome {
    Recorded,
    /// The channel is disabled by consent; nothing was stored.
    ChannelDisabled,
    /// The body violated shape or bounds; nothing was stored.
    InvalidRecord,
    /// The queue was full; the incoming record was dropped and counted.
    DroppedCapacity,
}

impl RecordOutcome {
    /// Reports whether the record entered the queue.
    #[must_use]
    pub const fn is_recorded(self) -> bool {
        matches!(self, Self::Recorded)
    }
}

/// The user-visible preview AND the transmitted payload: one string,
/// so they cannot diverge (PV-010).  Dropping the draft deletes it.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SendDraft {
    pub channel: DiagnosticsChannel,
    payload: String,
    record_count: usize,
}

impl SendDraft {
    /// Byte-exact content shown to the user before sending and handed to
    /// the transport afterwards.
    #[must_use]
    pub fn payload(&self) -> &str {
        &self.payload
    }

    /// Number of records folded into this draft.
    #[must_use]
    pub const fn record_count(&self) -> usize {
        self.record_count
    }
}

/// Bounded counters; all monotonic.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct OutboundStats {
    pub recorded_total: u64,
    pub dropped_capacity_total: u64,
    pub sent_records_total: u64,
    pub deleted_records_total: u64,
}

/// Outbound diagnostics queue governed by consent.
#[derive(Debug)]
pub struct OutboundDiagnostics {
    consent: DiagnosticsConsent,
    pending: VecDeque<PendingRecord>,
    stats: OutboundStats,
}

impl OutboundDiagnostics {
    /// Creates an empty queue under the given consent.
    #[must_use]
    pub fn new(consent: DiagnosticsConsent) -> Self {
        Self {
            consent,
            pending: VecDeque::new(),
            stats: OutboundStats::default(),
        }
    }

    #[must_use]
    pub const fn consent(&self) -> &DiagnosticsConsent {
        &self.consent
    }

    /// Updates consent.  Disabling a channel immediately deletes that
    /// channel's entire pending backlog; the cleared count is returned.
    pub fn set_consent(&mut self, channel: DiagnosticsChannel, enabled: bool) -> usize {
        let was_enabled = self.consent.allows(channel);
        self.consent.set(channel, enabled);
        if !enabled && was_enabled {
            self.clear_channel(channel)
        } else {
            0
        }
    }

    /// Queues one event body for its channel.  Bodies must be non-empty,
    /// within [`MAX_RECORD_BODY_BYTES`] and already redacted upstream.
    pub fn record(&mut self, record: PendingRecord) -> RecordOutcome {
        if !self.consent.allows(record.channel) {
            return RecordOutcome::ChannelDisabled;
        }
        if record.body.is_empty() || record.body.len() > MAX_RECORD_BODY_BYTES {
            return RecordOutcome::InvalidRecord;
        }
        if self.pending.len() >= MAX_PENDING_RECORDS {
            self.stats.dropped_capacity_total += 1;
            return RecordOutcome::DroppedCapacity;
        }
        self.pending.push_back(record);
        self.stats.recorded_total += 1;
        RecordOutcome::Recorded
    }

    /// Convenience wrapper rendering an accepted [`DiagnosticEvent`] into
    /// a deterministic single-line body (`channel|class|name`), then
    /// recording it.  Events classified `UserContent` or `Secret` are
    /// refused (`InvalidRecord`) — browsing URLs/titles and credentials
    /// can never enter an outbound body (PV-008).
    pub fn record_event(
        &mut self,
        channel: DiagnosticsChannel,
        event: &DiagnosticEvent,
    ) -> RecordOutcome {
        let Some(class) = class_wire(event.class()) else {
            return RecordOutcome::InvalidRecord;
        };
        let body = format!("{}|{}|{}", channel.wire_name(), class, event.name());
        self.record(PendingRecord { channel, body })
    }

    /// Removes up to `max_records` oldest pending records of an enabled
    /// channel and folds them into one draft.  Returns `None` when the
    /// channel is disabled (nothing may leave) or nothing is pending.
    /// Dropping the returned draft without sending equals deletion.
    #[must_use]
    pub fn drain_channel(
        &mut self,
        channel: DiagnosticsChannel,
        max_records: usize,
    ) -> Option<SendDraft> {
        if !self.consent.allows(channel) || max_records == 0 {
            return None;
        }
        let mut payload = String::new();
        let mut count = 0_usize;
        let mut rotations = 0_usize;
        while count < max_records {
            if self.pending.is_empty() {
                break;
            }
            let front = self.pending.front()?;
            if front.channel != channel {
                // Rotate past records of other channels without dropping;
                // stop once a full rotation found nothing for ours.
                let other = self.pending.pop_front()?;
                self.pending.push_back(other);
                rotations += 1;
                if rotations >= self.pending.len() {
                    break;
                }
                continue;
            }
            let record = self.pending.pop_front()?;
            payload.push_str(&record.body);
            payload.push('\n');
            count += 1;
            rotations = 0;
        }
        if count == 0 {
            return None;
        }
        self.stats.sent_records_total += count as u64;
        Some(SendDraft {
            channel,
            payload,
            record_count: count,
        })
    }

    /// Immediately deletes every pending record of one channel; returns
    /// the number deleted.
    pub fn clear_channel(&mut self, channel: DiagnosticsChannel) -> usize {
        let before = self.pending.len();
        self.pending.retain(|record| record.channel != channel);
        let removed = before - self.pending.len();
        self.stats.deleted_records_total += removed as u64;
        removed
    }

    /// Immediately deletes everything pending; returns the number deleted.
    pub fn clear_all(&mut self) -> usize {
        let removed = self.pending.len();
        self.pending.clear();
        self.stats.deleted_records_total += removed as u64;
        removed
    }

    #[must_use]
    pub const fn stats(&self) -> OutboundStats {
        self.stats
    }

    #[must_use]
    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }
}

fn class_wire(class: DataClass) -> Option<&'static str> {
    // Only non-content, non-secret classes may ever be queued; the
    // caller-side gate refuses the rest so browsing URLs/titles and
    // credentials cannot enter an outbound body (PV-008).
    match class {
        DataClass::Operational => Some("operational"),
        DataClass::Diagnostic => Some("diagnostic"),
        DataClass::UserContent | DataClass::Secret => None,
    }
}

#[cfg(test)]
#[path = "diagnostics_outbound_tests.rs"]
mod tests;
