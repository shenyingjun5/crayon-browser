//! `ManualClock`: deterministic logical time for TTL, retry, navigation
//! invalidation and session-timeout tests. No wall-clock reads, no sleeps.

use std::fmt::{Display, Formatter};
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll, Waker};
use std::time::Duration;

/// Maximum number of concurrently registered waiters (bounded queue rule).
const MAX_WAITERS: usize = 64;

/// Clock misuse surfaced to the test instead of hanging silently.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ClockError {
    /// More than `MAX_WAITERS` pending deadlines.
    WaiterLimitExceeded,
}

impl Display for ClockError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::WaiterLimitExceeded => f.write_str("manual clock waiter limit exceeded"),
        }
    }
}

impl std::error::Error for ClockError {}

#[derive(Default)]
struct ClockState {
    now: Duration,
    waiters: Vec<(Duration, Waker)>,
}

/// Shared logical clock. `advance` moves time forward and wakes every waiter
/// whose deadline has passed; `wait_until` never sleeps on the wall clock.
#[derive(Clone, Default)]
pub struct ManualClock {
    state: Arc<Mutex<ClockState>>,
}

impl ManualClock {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Current logical time (starts at zero).
    #[must_use]
    pub fn now(&self) -> Duration {
        self.state.lock().unwrap().now
    }

    /// Moves logical time forward; due waiters wake in deadline order.
    pub fn advance(&self, step: Duration) {
        let mut state = self.state.lock().unwrap();
        state.now += step;
        let now = state.now;
        let (mut fire, keep): (Vec<_>, Vec<_>) = state
            .waiters
            .drain(..)
            .partition(|(deadline, _)| *deadline <= now);
        fire.sort_by_key(|(deadline, _)| *deadline);
        state.waiters = keep;
        drop(state);
        for (_, waker) in fire {
            waker.wake();
        }
    }

    /// Number of pending waiters (test introspection).
    #[must_use]
    pub fn pending_waiters(&self) -> usize {
        self.state.lock().unwrap().waiters.len()
    }

    /// Resolves once logical time reaches `deadline`; resolves immediately if
    /// the deadline already passed.
    pub fn wait_until(&self, deadline: Duration) -> WaitUntil {
        WaitUntil {
            clock: self.clone(),
            deadline,
        }
    }
}

/// Future returned by [`ManualClock::wait_until`].
pub struct WaitUntil {
    clock: ManualClock,
    deadline: Duration,
}

impl Future for WaitUntil {
    type Output = Result<(), ClockError>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut state = self.clock.state.lock().unwrap();
        if state.now >= self.deadline {
            return Poll::Ready(Ok(()));
        }
        if state.waiters.len() >= MAX_WAITERS {
            return Poll::Ready(Err(ClockError::WaiterLimitExceeded));
        }
        let waker = cx.waker().clone();
        if !state.waiters.iter().any(|(_, w)| w.will_wake(&waker)) {
            state.waiters.push((self.deadline, waker));
        }
        Poll::Pending
    }
}
