//! Deterministic behaviour tests for the client handoff adapter.

use super::*;
use crayon_platform_api::external_client_handoff::{HandoffOutcome as Outcome, HandoffReason};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex};

const HTTPS_PAGE: &str = "https://example.invalid/crayon-cast-client";

fn recorder() -> (Arc<Mutex<Vec<LaunchTarget>>>, Arc<AtomicU32>, ShellOpen) {
    let calls: Arc<Mutex<Vec<LaunchTarget>>> = Arc::new(Mutex::new(Vec::new()));
    let failures = Arc::new(AtomicU32::new(0));
    let calls_for_hook = Arc::clone(&calls);
    let failures_for_hook = Arc::clone(&failures);
    let hook: ShellOpen = Box::new(move |target| {
        // Simulate a refusal when flagged; otherwise record success.
        if failures_for_hook.load(Ordering::SeqCst) > 0 {
            return Err(ExecuteFailure::Refused);
        }
        calls_for_hook.lock().expect("calls").push(target.clone());
        Ok(())
    });
    (calls, failures, hook)
}

#[test]
fn https_url_is_required_upfront() {
    let result = WindowsClientHandoff::new(PathBuf::from("x"), "http://insecure.example");
    assert!(result.is_err(), "non-https download pages are rejected");
}

#[test]
fn download_opens_the_injected_https_page() {
    let (calls, _failures, hook) = recorder();
    let mut handoff =
        WindowsClientHandoff::with_executor(PathBuf::from("whatever"), HTTPS_PAGE.into(), hook);
    let request = HandoffRequest::new(
        HandoffReason::NoRouteAvailable,
        HandoffAction::DownloadClient,
        "diag-token",
    )
    .expect("valid request");
    assert_eq!(
        ExternalClientHandoff::perform(&mut handoff, &request),
        Ok(Outcome::DownloadStarted)
    );
    assert_eq!(
        *calls.lock().expect("calls"),
        vec![LaunchTarget::Url(HTTPS_PAGE.into())]
    );
}

#[test]
fn missing_client_reports_not_installed_without_shell_call() {
    let (calls, _f, hook) = recorder();
    let mut handoff = WindowsClientHandoff::with_executor(
        PathBuf::from("Z:/definitely/absent/client.exe"),
        HTTPS_PAGE.into(),
        hook,
    );
    let request = HandoffRequest::new(
        HandoffReason::UserChoice,
        HandoffAction::LaunchClient,
        "diag-token",
    )
    .expect("valid request");
    assert_eq!(
        ExternalClientHandoff::perform(&mut handoff, &request),
        Ok(Outcome::NotInstalled)
    );
    assert!(calls.lock().expect("calls").is_empty());
}

#[test]
fn present_client_launches_and_refusal_maps_to_unavailable() {
    let existing = std::env::temp_dir().join("crayon-w04d-client-marker.exe");
    std::fs::write(&existing, b"marker").expect("marker file");
    let (calls, failures, hook) = recorder();

    let mut handoff =
        WindowsClientHandoff::with_executor(existing.clone(), HTTPS_PAGE.into(), hook);
    let request = HandoffRequest::new(
        HandoffReason::UserChoice,
        HandoffAction::LaunchClient,
        "diag-token",
    )
    .expect("valid request");
    assert_eq!(
        ExternalClientHandoff::perform(&mut handoff, &request),
        Ok(Outcome::LaunchRequested)
    );
    assert_eq!(
        *calls.lock().expect("calls"),
        vec![LaunchTarget::Executable(existing.clone())]
    );

    failures.store(1, Ordering::SeqCst);
    assert_eq!(
        ExternalClientHandoff::perform(&mut handoff, &request),
        Err(HandoffError::Unavailable),
        "shell refusal surfaces as failure, never as fake success"
    );
    let _ = std::fs::remove_file(&existing);
    assert!(!calls.lock().expect("calls").is_empty());
}
