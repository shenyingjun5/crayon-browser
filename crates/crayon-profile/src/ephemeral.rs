//! Ephemeral (incognito) profile lifecycle and cleanup manifest.
//!
//! An ephemeral session tracks its window count; closing the last window
//! starts the cleanup of a closed set of storage categories.  Actual
//! deletion is performed by an executor injected by the caller (the CEF
//! adapter wires real cache/cookie storage later); this module owns the
//! state machine, the manifest and the per-category result report.
//!
//! Red line: a failed cleanup is always reported explicitly — nothing here
//! ever claims "cleared" for a best-effort or failed outcome.

use crate::ProfileId;
use std::collections::BTreeMap;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Closed set of storage categories an ephemeral session must clean up.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum CleanupCategory {
    HttpCache,
    DomStorage,
    CookiesAndSiteData,
    FileSystemAccess,
    MediaState,
}

impl CleanupCategory {
    /// The full cleanup manifest, in stable order.
    pub const ALL: [Self; 5] = [
        Self::HttpCache,
        Self::DomStorage,
        Self::CookiesAndSiteData,
        Self::FileSystemAccess,
        Self::MediaState,
    ];
}

/// Per-category cleanup outcome reported by the injected executor.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CleanupOutcome {
    /// The category's data was actually removed.
    Cleared,
    /// The category held no data for this session.
    NotPresent,
    /// Removal failed; the report must surface this explicitly.
    Failed,
}

/// Deletes one storage category for the session.  Injected by the caller so
/// this module never touches storage itself.
pub trait CleanupExecutor {
    fn cleanup(&mut self, category: CleanupCategory) -> CleanupOutcome;
}

/// Lifecycle state of an ephemeral session.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EphemeralState {
    Active,
    /// Last window closed; cleanup has not completed yet.
    Closing,
    CleaningUp,
    /// Fully cleaned; terminal.
    Disposed,
}

/// Ephemeral-session command failure.  Stable variants, no internal detail.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EphemeralError {
    /// The current state does not allow the operation.
    IllegalState,
    /// Window count would go negative.
    WindowUnderflow,
    /// Cleanup finished with at least one `Failed` category; the session
    /// stays in `CleaningUp` and may be retried.
    CleanupIncomplete,
}

impl Display for EphemeralError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::IllegalState => "ephemeral session state rejects the operation",
            Self::WindowUnderflow => "cannot close more windows than are open",
            Self::CleanupIncomplete => "cleanup finished with failed categories",
        };
        formatter.write_str(message)
    }
}

impl Error for EphemeralError {}

/// Explicit cleanup report.  `fully_cleared()` is the only source of the
/// "incognito data is gone" claim; it is true only when every category is
/// `Cleared` or `NotPresent`.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CleanupReport {
    outcomes: BTreeMap<CleanupCategory, CleanupOutcome>,
}

impl CleanupReport {
    /// Reports whether every category was cleared or was absent.
    #[must_use]
    pub fn fully_cleared(&self) -> bool {
        CleanupCategory::ALL.iter().all(|category| {
            matches!(
                self.outcomes.get(category),
                Some(CleanupOutcome::Cleared | CleanupOutcome::NotPresent)
            )
        })
    }

    /// Outcome of one category; `None` means the category never ran.
    #[must_use]
    pub fn outcome_of(&self, category: CleanupCategory) -> Option<CleanupOutcome> {
        self.outcomes.get(&category).copied()
    }

    /// Categories whose cleanup explicitly failed.
    #[must_use]
    pub fn failed_categories(&self) -> Vec<CleanupCategory> {
        self.outcomes
            .iter()
            .filter(|(_, outcome)| **outcome == CleanupOutcome::Failed)
            .map(|(category, _)| *category)
            .collect()
    }
}

/// Window-counted lifecycle and cleanup owner for one ephemeral profile.
pub struct EphemeralSession {
    profile_id: ProfileId,
    state: EphemeralState,
    open_windows: u32,
    last_report: Option<CleanupReport>,
}

impl EphemeralSession {
    /// Starts a session for an incognito profile with no windows yet.
    #[must_use]
    pub fn new(profile_id: ProfileId) -> Self {
        Self {
            profile_id,
            state: EphemeralState::Active,
            open_windows: 0,
            last_report: None,
        }
    }

    #[must_use]
    pub const fn profile_id(&self) -> &ProfileId {
        &self.profile_id
    }

    #[must_use]
    pub const fn state(&self) -> EphemeralState {
        self.state
    }

    #[must_use]
    pub const fn open_windows(&self) -> u32 {
        self.open_windows
    }

    /// The most recent cleanup report, if cleanup has run.
    #[must_use]
    pub fn last_report(&self) -> Option<&CleanupReport> {
        self.last_report.as_ref()
    }

    /// Registers an opened window.  Once closing has begun, no new windows
    /// may open.
    pub fn open_window(&mut self) -> Result<(), EphemeralError> {
        if self.state != EphemeralState::Active {
            return Err(EphemeralError::IllegalState);
        }
        self.open_windows = self
            .open_windows
            .checked_add(1)
            .ok_or(EphemeralError::IllegalState)?;
        Ok(())
    }

    /// Registers a closed window.  Closing the last window moves the session
    /// to `Closing`.  Underflow and closes after disposal are rejected.
    pub fn close_window(&mut self) -> Result<(), EphemeralError> {
        match self.state {
            EphemeralState::Active => {
                if self.open_windows == 0 {
                    return Err(EphemeralError::WindowUnderflow);
                }
                self.open_windows -= 1;
                if self.open_windows == 0 {
                    self.state = EphemeralState::Closing;
                }
                Ok(())
            }
            EphemeralState::Closing | EphemeralState::CleaningUp | EphemeralState::Disposed => {
                Err(EphemeralError::IllegalState)
            }
        }
    }

    /// Runs the cleanup manifest through the injected executor.
    ///
    /// Requires `Closing` (first run) or `CleaningUp` (retry).  On full
    /// success the session becomes `Disposed`; on any failure the session
    /// stays in `CleaningUp`, the explicit report is stored, and
    /// `CleanupIncomplete` is returned — the failure is never masked.
    pub fn run_cleanup(
        &mut self,
        executor: &mut dyn CleanupExecutor,
    ) -> Result<(), EphemeralError> {
        match self.state {
            EphemeralState::Closing | EphemeralState::CleaningUp => {}
            EphemeralState::Active | EphemeralState::Disposed => {
                return Err(EphemeralError::IllegalState);
            }
        }
        self.state = EphemeralState::CleaningUp;
        let mut outcomes = BTreeMap::new();
        for category in CleanupCategory::ALL {
            outcomes.insert(category, executor.cleanup(category));
        }
        let report = CleanupReport { outcomes };
        let cleared = report.fully_cleared();
        self.last_report = Some(report);
        if cleared {
            self.state = EphemeralState::Disposed;
            Ok(())
        } else {
            Err(EphemeralError::CleanupIncomplete)
        }
    }

    /// Retries cleanup after a partial failure.  Equivalent to
    /// `run_cleanup`; provided as the intent-revealing entry point.
    pub fn retry_cleanup(
        &mut self,
        executor: &mut dyn CleanupExecutor,
    ) -> Result<(), EphemeralError> {
        self.run_cleanup(executor)
    }
}
