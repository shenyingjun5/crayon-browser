//! Bounded effect verification and idempotency (ACT-08, AC-008).
//!
//! The effect layer owns the terminal semantics of one executed action:
//! only `verified` reports success, `indeterminate` is terminal and never
//! replayed, and an idempotency key can produce exactly one effect record
//! — a repeated key returns the frozen prior report instead of re-running
//! the action. Waits are bounded by construction; the clock is injected.

use crate::handle::ActionHandleId;
use crayon_domain::{EffectOutcome, EffectReport, SemanticSchemaError};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// Maximum number of effect records one ledger keeps.
pub const MAX_EFFECT_LEDGER: usize = 256;

/// Maximum bounded wait for one effect observation, in milliseconds.
pub const MAX_EFFECT_WAIT_MS: u64 = 10_000;

/// Maximum length of an idempotency key token.
pub const MAX_IDEMPOTENCY_KEY_BYTES: usize = 64;

/// Invalid idempotency key.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IdempotencyKeyError {
    Empty,
    TooLong,
    InvalidCharset,
}

impl std::fmt::Display for IdempotencyKeyError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Empty => formatter.write_str("idempotency key must not be empty"),
            Self::TooLong => formatter.write_str("idempotency key exceeds the maximum length"),
            Self::InvalidCharset => {
                formatter.write_str("idempotency key contains characters outside [a-z0-9_.:-]")
            }
        }
    }
}

impl std::error::Error for IdempotencyKeyError {}

/// Opaque, client-chosen idempotency key with a closed charset; the same
/// key always refers to the same intended action, across transport retries.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct IdempotencyKey(String);

impl IdempotencyKey {
    /// Wraps a validated idempotency key.
    pub fn new(raw: &str) -> Result<Self, IdempotencyKeyError> {
        if raw.is_empty() {
            return Err(IdempotencyKeyError::Empty);
        }
        if raw.len() > MAX_IDEMPOTENCY_KEY_BYTES {
            return Err(IdempotencyKeyError::TooLong);
        }
        if !raw
            .bytes()
            .all(|b| matches!(b, b'a'..=b'z' | b'0'..=b'9' | b'_' | b'.' | b':' | b'-'))
        {
            return Err(IdempotencyKeyError::InvalidCharset);
        }
        Ok(Self(raw.to_owned()))
    }

    /// Derives the key for one handle execution: handles are single-use,
    /// so the handle id is a sufficient idempotency scope.
    #[must_use]
    pub fn for_handle(handle_id: &ActionHandleId) -> Self {
        Self(format!("h.{}", handle_id.as_str()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl TryFrom<String> for IdempotencyKey {
    type Error = IdempotencyKeyError;

    fn try_from(raw: String) -> Result<Self, Self::Error> {
        Self::new(&raw)
    }
}

impl From<IdempotencyKey> for String {
    fn from(key: IdempotencyKey) -> Self {
        key.0
    }
}

/// Bounded wait window for observing one effect.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EffectWaitSpec {
    pub now_ms: u64,
    pub deadline_ms: u64,
}

impl EffectWaitSpec {
    /// Creates a bounded wait; zero or over-budget waits fail closed.
    pub fn new(now_ms: u64, deadline_ms: u64) -> Result<Self, SemanticSchemaError> {
        if deadline_ms <= now_ms || deadline_ms - now_ms > MAX_EFFECT_WAIT_MS {
            return Err(SemanticSchemaError::BudgetExceeded("effect wait"));
        }
        Ok(Self {
            now_ms,
            deadline_ms,
        })
    }

    /// Whether the bounded wait has elapsed at the injected reading.
    #[must_use]
    pub const fn elapsed_at(&self, now_ms: u64) -> bool {
        now_ms >= self.deadline_ms
    }
}

/// Outcome of checking a key before execution.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CheckOutcome<'a> {
    /// No prior record; the caller may execute exactly once and must call
    /// `record` with the terminal report.
    Fresh,
    /// A terminal report already exists for the key; the action must not
    /// re-run. Applies to `verified` and `failed` alike.
    AlreadyReported(&'a EffectReport),
    /// A prior `indeterminate` report exists; replay is forbidden because
    /// the side effect may or may not have happened.
    IndeterminateBlocked,
}

/// Single owner of terminal effect records, keyed by idempotency key.
#[derive(Debug, Default)]
pub struct EffectLedger {
    reports: BTreeMap<IdempotencyKey, EffectReport>,
}

impl EffectLedger {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Number of frozen records.
    #[must_use]
    pub fn len(&self) -> usize {
        self.reports.len()
    }

    /// Whether no record is held.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.reports.is_empty()
    }

    /// Checks a key before execution; a repeated key never re-runs.
    #[must_use]
    pub fn check(&self, key: &IdempotencyKey) -> CheckOutcome<'_> {
        match self.reports.get(key) {
            Some(report) if report.outcome == EffectOutcome::Indeterminate => {
                CheckOutcome::IndeterminateBlocked
            }
            Some(report) => CheckOutcome::AlreadyReported(report),
            None => CheckOutcome::Fresh,
        }
    }

    /// Freezes the terminal report of one executed action. The report must
    /// be terminal-consistent (enforced by `EffectReport::new`) and the key
    /// must not already carry a record.
    pub fn record(
        &mut self,
        key: IdempotencyKey,
        report: EffectReport,
    ) -> Result<(), SemanticSchemaError> {
        if self.reports.contains_key(&key) {
            return Err(SemanticSchemaError::DuplicateEntry("effect record"));
        }
        if self.reports.len() >= MAX_EFFECT_LEDGER {
            return Err(SemanticSchemaError::BudgetExceeded("effect ledger"));
        }
        self.reports.insert(key, report);
        Ok(())
    }

    /// Whether this outcome counts as success; only `verified` does.
    #[must_use]
    pub const fn is_success(outcome: EffectOutcome) -> bool {
        matches!(outcome, EffectOutcome::Verified)
    }
}
