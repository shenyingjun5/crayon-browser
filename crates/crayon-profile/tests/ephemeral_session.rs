//! Behaviour tests for the ephemeral (incognito) session lifecycle and
//! cleanup manifest.  Executors are deterministic fixtures.

use crayon_profile::{
    CleanupCategory, CleanupExecutor, CleanupOutcome, EphemeralError, EphemeralSession,
    EphemeralState, ProfileId,
};
use std::collections::BTreeMap;

/// Deterministic executor fixture: maps each category to a fixed outcome.
struct FixtureExecutor {
    outcomes: BTreeMap<CleanupCategory, CleanupOutcome>,
    calls: Vec<CleanupCategory>,
}

impl FixtureExecutor {
    fn all_cleared() -> Self {
        Self::with_overrides(&[])
    }

    fn with_overrides(overrides: &[(CleanupCategory, CleanupOutcome)]) -> Self {
        let mut outcomes = BTreeMap::new();
        for category in CleanupCategory::ALL {
            outcomes.insert(category, CleanupOutcome::Cleared);
        }
        for (category, outcome) in overrides {
            outcomes.insert(*category, *outcome);
        }
        Self {
            outcomes,
            calls: Vec::new(),
        }
    }
}

impl CleanupExecutor for FixtureExecutor {
    fn cleanup(&mut self, category: CleanupCategory) -> CleanupOutcome {
        self.calls.push(category);
        self.outcomes[&category]
    }
}

fn session() -> EphemeralSession {
    EphemeralSession::new(ProfileId::new("private").expect("id"))
}

// ---------- Window lifecycle ----------

#[test]
fn window_open_close_cycle() {
    let mut session = session();
    assert_eq!(session.state(), EphemeralState::Active);
    session.open_window().expect("open 1");
    session.open_window().expect("open 2");
    assert_eq!(session.open_windows(), 2);
    session.close_window().expect("close 1");
    assert_eq!(session.state(), EphemeralState::Active);
    session.close_window().expect("close last");
    assert_eq!(session.state(), EphemeralState::Closing);
}

#[test]
fn close_underflow_rejected() {
    let mut session = session();
    assert_eq!(session.close_window(), Err(EphemeralError::WindowUnderflow));
    assert_eq!(session.state(), EphemeralState::Active);
}

#[test]
fn no_new_windows_after_closing_begins() {
    let mut session = session();
    session.open_window().expect("open");
    session.close_window().expect("close last");
    assert_eq!(session.open_window(), Err(EphemeralError::IllegalState));
    assert_eq!(
        session.close_window(),
        Err(EphemeralError::IllegalState) // no windows left to close
    );
}

// ---------- Cleanup ----------

#[test]
fn cleanup_requires_closing_state() {
    let mut session = session();
    session.open_window().expect("open");
    let mut executor = FixtureExecutor::all_cleared();
    assert_eq!(
        session.run_cleanup(&mut executor),
        Err(EphemeralError::IllegalState) // still active
    );
}

#[test]
fn full_cleanup_disposes_session() {
    let mut session = session();
    session.open_window().expect("open");
    session.close_window().expect("close last");
    let mut executor = FixtureExecutor::all_cleared();
    session.run_cleanup(&mut executor).expect("cleanup");
    assert_eq!(session.state(), EphemeralState::Disposed);
    // Every manifest category ran exactly once, in stable order.
    assert_eq!(executor.calls, CleanupCategory::ALL);
    let report = session.last_report().expect("report");
    assert!(report.fully_cleared());
    assert!(report.failed_categories().is_empty());
}

#[test]
fn not_present_categories_count_as_cleared() {
    let mut session = session();
    session.open_window().expect("open");
    session.close_window().expect("close last");
    let mut executor = FixtureExecutor::with_overrides(&[
        (CleanupCategory::HttpCache, CleanupOutcome::NotPresent),
        (CleanupCategory::MediaState, CleanupOutcome::NotPresent),
    ]);
    session.run_cleanup(&mut executor).expect("cleanup");
    assert_eq!(session.state(), EphemeralState::Disposed);
    assert!(session.last_report().expect("report").fully_cleared());
    assert_eq!(
        session
            .last_report()
            .expect("report")
            .outcome_of(CleanupCategory::HttpCache),
        Some(CleanupOutcome::NotPresent)
    );
}

#[test]
fn failed_cleanup_is_explicit_and_retryable() {
    let mut session = session();
    session.open_window().expect("open");
    session.close_window().expect("close last");

    let mut failing = FixtureExecutor::with_overrides(&[(
        CleanupCategory::CookiesAndSiteData,
        CleanupOutcome::Failed,
    )]);
    assert_eq!(
        session.run_cleanup(&mut failing),
        Err(EphemeralError::CleanupIncomplete)
    );
    // Failure is never masked: session is not disposed and the report
    // names the failed category.
    assert_eq!(session.state(), EphemeralState::CleaningUp);
    let report = session.last_report().expect("report");
    assert!(!report.fully_cleared());
    assert_eq!(
        report.failed_categories(),
        vec![CleanupCategory::CookiesAndSiteData]
    );

    // Retry with a now-working executor succeeds.
    let mut recovered = FixtureExecutor::all_cleared();
    session.retry_cleanup(&mut recovered).expect("retry");
    assert_eq!(session.state(), EphemeralState::Disposed);
    assert!(session.last_report().expect("report").fully_cleared());
}

#[test]
fn disposed_session_rejects_everything() {
    let mut session = session();
    session.open_window().expect("open");
    session.close_window().expect("close last");
    let mut executor = FixtureExecutor::all_cleared();
    session.run_cleanup(&mut executor).expect("cleanup");

    assert_eq!(session.open_window(), Err(EphemeralError::IllegalState));
    assert_eq!(session.close_window(), Err(EphemeralError::IllegalState));
    assert_eq!(
        session.run_cleanup(&mut executor),
        Err(EphemeralError::IllegalState)
    );
}

#[test]
fn repeated_cleanup_attempts_update_report() {
    let mut session = session();
    session.open_window().expect("open");
    session.close_window().expect("close last");

    let mut failing = FixtureExecutor::with_overrides(&[(
        CleanupCategory::FileSystemAccess,
        CleanupOutcome::Failed,
    )]);
    assert!(session.run_cleanup(&mut failing).is_err());
    assert!(session.run_cleanup(&mut failing).is_err()); // idempotent retry
    assert_eq!(
        session.last_report().expect("report").failed_categories(),
        vec![CleanupCategory::FileSystemAccess]
    );
    assert_eq!(session.state(), EphemeralState::CleaningUp);
}
