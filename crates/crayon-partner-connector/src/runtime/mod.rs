//! Connector call runtime policy (HUB-14).
//!
//! Pure, clock-injected policies for the outbound call path: call quota
//! with a minimum spacing, a circuit breaker (closed -> open -> half-open
//! -> closed), a retry budget where side-effectful calls never retry, and
//! a cooperative cancellation flag. No threads, no timers, no persistence.

/// Why a call was shed or refused. Content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RuntimeRejection {
    /// The per-window quota is exhausted.
    QuotaExhausted,
    /// The call arrived before the minimum spacing elapsed.
    TooSoon,
    /// The circuit is open: the connector is known-bad.
    CircuitOpen,
    /// The retry budget is exhausted.
    RetryBudgetExhausted,
}

/// Sliding-window call quota plus a minimum spacing between calls.
/// Clock-injected (`now_ms`); no timers.
#[derive(Clone, Debug)]
pub struct CallLimiter {
    window_ms: u64,
    max_calls_per_window: usize,
    min_spacing_ms: u64,
    recent: Vec<u64>,
}

impl CallLimiter {
    #[must_use]
    pub fn new(window_ms: u64, max_calls_per_window: usize, min_spacing_ms: u64) -> Self {
        Self {
            window_ms,
            max_calls_per_window,
            min_spacing_ms,
            recent: Vec::new(),
        }
    }

    /// Admits one call at |now_ms| or sheds it. Accepted timestamps are
    /// recorded; old entries outside the window are dropped.
    pub fn admit(&mut self, now_ms: u64) -> Result<(), RuntimeRejection> {
        self.recent
            .retain(|t| now_ms.saturating_sub(*t) < self.window_ms);
        if self.recent.len() >= self.max_calls_per_window {
            return Err(RuntimeRejection::QuotaExhausted);
        }
        if let Some(last) = self.recent.last() {
            if now_ms.saturating_sub(*last) < self.min_spacing_ms {
                return Err(RuntimeRejection::TooSoon);
            }
        }
        self.recent.push(now_ms);
        self.recent.sort_unstable();
        Ok(())
    }
}

/// Circuit breaker states.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CircuitState {
    /// Calls flow normally.
    Closed,
    /// Failures crossed the threshold: every call is refused.
    Open,
    /// One probe call is admitted; success closes, failure re-opens.
    HalfOpen,
}

/// Three-state circuit breaker over consecutive failures.
#[derive(Clone, Copy, Debug)]
pub struct CircuitBreaker {
    failure_threshold: usize,
    cooldown_ms: u64,
    state: CircuitState,
    consecutive_failures: usize,
    opened_at_ms: u64,
    probe_used: bool,
}

impl CircuitBreaker {
    #[must_use]
    pub fn new(failure_threshold: usize, cooldown_ms: u64) -> Self {
        Self {
            failure_threshold: failure_threshold.max(1),
            cooldown_ms,
            state: CircuitState::Closed,
            consecutive_failures: 0,
            opened_at_ms: 0,
            probe_used: false,
        }
    }

    #[must_use]
    pub const fn state(&self) -> CircuitState {
        self.state
    }

    /// Admits or refuses a call at |now_ms|.
    pub fn admit(&mut self, now_ms: u64) -> Result<(), RuntimeRejection> {
        match self.state {
            CircuitState::Closed => Ok(()),
            CircuitState::Open => {
                if now_ms.saturating_sub(self.opened_at_ms) >= self.cooldown_ms {
                    self.state = CircuitState::HalfOpen;
                    self.probe_used = false;
                    self.admit(now_ms)
                } else {
                    Err(RuntimeRejection::CircuitOpen)
                }
            }
            CircuitState::HalfOpen => {
                if self.probe_used {
                    Err(RuntimeRejection::CircuitOpen)
                } else {
                    self.probe_used = true;
                    Ok(())
                }
            }
        }
    }

    /// Records a successful call: closes the circuit from any state.
    pub fn on_success(&mut self) {
        self.consecutive_failures = 0;
        self.state = CircuitState::Closed;
        self.probe_used = false;
    }

    /// Records a failed call. In Closed: opens after the threshold. In
    /// HalfOpen: re-opens immediately.
    pub fn on_failure(&mut self, now_ms: u64) {
        self.consecutive_failures += 1;
        if self.state == CircuitState::HalfOpen
            || self.consecutive_failures >= self.failure_threshold
        {
            self.state = CircuitState::Open;
            self.opened_at_ms = now_ms;
        }
    }
}

/// Retry budget for one call family. Side-effectful calls (mutations)
/// never retry; idempotent reads may retry while the budget lasts.
#[derive(Clone, Copy, Debug)]
pub struct RetryBudget {
    max_retries: usize,
    remaining: usize,
}

impl RetryBudget {
    /// `max_retries` bounds retries per budget cycle (>= 1).
    #[must_use]
    pub fn new(max_retries: usize) -> Self {
        Self {
            max_retries: max_retries.max(1),
            remaining: max_retries.max(1),
        }
    }

    /// Whether a retry is permitted. `is_idempotent_read` gates
    /// everything: mutations never retry.
    #[must_use]
    pub const fn can_retry(&self, is_idempotent_read: bool) -> bool {
        is_idempotent_read && self.remaining > 0
    }

    /// Consumes one retry from the budget.
    pub fn consume(&mut self) {
        self.remaining = self.remaining.saturating_sub(1);
    }

    /// Refills the budget to its maximum.
    pub fn refill(&mut self) {
        self.remaining = self.max_retries;
    }

    #[must_use]
    pub const fn remaining(&self) -> usize {
        self.remaining
    }
}

/// Cooperative cancellation flag polled by the executing host port.
#[derive(Clone, Default)]
pub struct CancellationFlag(Arc<AtomicBool>);

impl CancellationFlag {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn cancel(&self) {
        self.0.store(true, Ordering::SeqCst);
    }

    #[must_use]
    pub fn is_cancelled(&self) -> bool {
        self.0.load(Ordering::SeqCst)
    }
}

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

#[cfg(test)]
mod runtime_tests;
