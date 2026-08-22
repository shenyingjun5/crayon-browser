//! Power and session lifecycle observation.
//!
//! CP-004 contract: `Suspending` and `SessionEnding` terminate live
//! sessions; after `Resumed` or a lock/unlock cycle, previously live
//! cast/agent sessions must NOT silently resume — owning modules
//! re-validate and let the user reconnect.  This interface only transports
//! the events that drive that re-validation.

use std::error::Error;
use std::fmt::{Display, Formatter};

/// Closed power/session lifecycle events.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LifecycleEvent {
    /// The system is about to sleep.
    Suspending,
    /// The system woke from sleep.
    Resumed,
    /// The screen or session locked.
    ScreenLocked,
    /// The screen or session unlocked.
    ScreenUnlocked,
    /// The OS session is ending (logoff/shutdown).
    SessionEnding,
}

/// Reports whether the event terminates live sessions (CP-004): suspending
/// and session-ending events invalidate every active session.
#[must_use]
pub const fn is_session_terminating(event: LifecycleEvent) -> bool {
    matches!(
        event,
        LifecycleEvent::Suspending | LifecycleEvent::SessionEnding
    )
}

/// Lifecycle observation failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LifecycleError {
    /// Lifecycle event observation is unavailable on this platform state.
    Unavailable,
}

impl Display for LifecycleError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Unavailable => formatter.write_str("lifecycle observation unavailable"),
        }
    }
}

impl Error for LifecycleError {}

/// Power/session lifecycle event source.
pub trait PowerLifecycleMonitor: Send {
    /// Registers or replaces the event listener; `None` unsubscribes.
    /// Delivery stops when the adapter shuts down.
    fn set_listener(
        &mut self,
        listener: Option<Box<dyn FnMut(LifecycleEvent) + Send>>,
    ) -> Result<(), LifecycleError>;
}

#[cfg(test)]
#[path = "lifecycle_tests.rs"]
mod tests;
