//! Deterministic behaviour tests for the update-flow driver.

use super::*;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

fn counters() -> (Arc<AtomicU32>, Arc<AtomicU32>, Arc<AtomicU32>) {
    let n = || Arc::new(AtomicU32::new(0));
    (n(), n(), n())
}

fn operations(
    check_outcome: CheckOutcome,
    counts: &(Arc<AtomicU32>, Arc<AtomicU32>, Arc<AtomicU32>),
) -> UpdateOperations {
    let (c, d, i) = counts;
    let c2 = Arc::clone(c);
    let d2 = Arc::clone(d);
    let i2 = Arc::clone(i);
    UpdateOperations {
        check: Box::new(move || {
            c2.fetch_add(1, Ordering::SeqCst);
            check_outcome
        }),
        download: Box::new(move || {
            d2.fetch_add(1, Ordering::SeqCst);
            true
        }),
        install: Box::new(move || {
            i2.fetch_add(1, Ordering::SeqCst);
            true
        }),
    }
}

#[test]
fn happy_check_available_download_install_cycle() {
    let counts = counters();
    let mut flow = WindowsUpdateFlow::new(operations(CheckOutcome::UpdateAvailable, &counts));
    assert_eq!(flow.state(), UpdateState::Idle);
    assert_eq!(
        flow.dispatch(UpdateCommand::StartCheck),
        Ok(UpdateState::Available)
    );
    assert_eq!(
        flow.dispatch(UpdateCommand::StartDownload),
        Ok(UpdateState::ReadyToInstall)
    );
    assert_eq!(flow.dispatch(UpdateCommand::Install), Ok(UpdateState::Idle));
    // Each injected operation ran exactly once.
    assert_eq!(counts.0.load(Ordering::SeqCst), 1);
    assert_eq!(counts.1.load(Ordering::SeqCst), 1);
    assert_eq!(counts.2.load(Ordering::SeqCst), 1);
}

#[test]
fn failed_check_lands_in_failed_and_dismisses() {
    let counts = counters();
    let mut flow = WindowsUpdateFlow::new(operations(CheckOutcome::Failure, &counts));
    assert_eq!(
        flow.dispatch(UpdateCommand::StartCheck),
        Ok(UpdateState::Failed)
    );
    // Download from Failed is illegal and leaves state unchanged.
    assert!(flow.dispatch(UpdateCommand::StartDownload).is_err());
    assert_eq!(flow.state(), UpdateState::Failed);
    assert_eq!(
        flow.dispatch(UpdateCommand::DismissFailure),
        Ok(UpdateState::Idle)
    );
    assert_eq!(counts.1.load(Ordering::SeqCst), 0, "no download attempted");
}

#[test]
fn no_update_returns_to_idle_without_operations() {
    let counts = counters();
    let mut flow = WindowsUpdateFlow::new(operations(CheckOutcome::NoUpdate, &counts));
    assert_eq!(
        flow.dispatch(UpdateCommand::StartCheck),
        Ok(UpdateState::Idle)
    );
    // Install is illegal in Idle; the injected hook must not run.
    assert!(flow.dispatch(UpdateCommand::Install).is_err());
    assert_eq!(counts.2.load(Ordering::SeqCst), 0);
}
