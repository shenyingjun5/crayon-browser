//! Skill health tracking (WFL-13).
//!
//! Pure, clock-injected failure-window policy: when a skill accumulates
//! the configured failure count inside the sliding window, the host should
//! disable it. Successes reset the consecutive-failure streak. In-memory
//! only; restart resets health (the store keeps the persisted state).

use std::collections::BTreeMap;

/// Failure-window policy defaults.
pub const FAILURE_WINDOW_THRESHOLD: usize = 3;
pub const FAILURE_WINDOW_MS: u64 = 10 * 60 * 1000;

/// Health verdict for one skill.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HealthVerdict {
    Healthy,
    /// Failures crossed the threshold inside the window: the host should
    /// disable the skill (WFL-13 health-disable).
    ShouldDisable,
}

/// Per-skill failure-window tracker.
#[derive(Default)]
pub struct SkillHealth {
    entries: BTreeMap<String, Vec<u64>>,
    threshold: usize,
    window_ms: u64,
}

impl SkillHealth {
    #[must_use]
    pub fn new(threshold: usize, window_ms: u64) -> Self {
        Self {
            entries: BTreeMap::new(),
            threshold: threshold.max(1),
            window_ms,
        }
    }

    /// Default policy thresholds.
    #[must_use]
    pub fn with_defaults() -> Self {
        Self::new(FAILURE_WINDOW_THRESHOLD, FAILURE_WINDOW_MS)
    }

    /// Records one failure at |now_ms| and returns the verdict.
    pub fn record_failure(&mut self, name: &str, now_ms: u64) -> HealthVerdict {
        let stamps = self.entries.entry(name.to_owned()).or_default();
        stamps.retain(|t| now_ms.saturating_sub(*t) < self.window_ms);
        stamps.push(now_ms);
        if stamps.len() >= self.threshold {
            HealthVerdict::ShouldDisable
        } else {
            HealthVerdict::Healthy
        }
    }

    /// Records a success: clears the streak and drops the entry when
    /// nothing is tracked anymore.
    pub fn record_success(&mut self, name: &str) {
        self.entries.remove(name);
    }

    /// Clears health state after the host disabled the skill (re-enabling
    /// via the store starts from a clean slate).
    pub fn reset(&mut self, name: &str) {
        self.entries.remove(name);
    }

    /// Recent failure count for diagnostics.
    #[must_use]
    pub fn recent_failures(&self, name: &str, now_ms: u64) -> usize {
        self.entries
            .get(name)
            .map(|stamps| {
                stamps
                    .iter()
                    .filter(|t| now_ms.saturating_sub(**t) < self.window_ms)
                    .count()
            })
            .unwrap_or(0)
    }
}

#[cfg(test)]
mod health_tests;
