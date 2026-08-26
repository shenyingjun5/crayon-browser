//! M04d external-client handoff tests.

use super::*;
use crayon_platform_api::external_client_handoff::{
    ExternalClientHandoff, HandoffAction, HandoffError, HandoffOutcome, HandoffReason,
    HandoffRequest,
};

fn make_request(action: HandoffAction) -> HandoffRequest {
    HandoffRequest::new(HandoffReason::NoRouteAvailable, action, "cast-handoff")
        .expect("valid request")
}

#[test]
fn launch_client_succeeds() {
    let mut adapter = MacClientHandoff::new(
        LaunchTarget::Executable("/Applications/CrayonCast.app".into()),
        "https://crayon.example/download".into(),
        Box::new(|_| true),
    );
    let request = make_request(HandoffAction::LaunchClient);
    assert_eq!(
        adapter.perform(&request),
        Ok(HandoffOutcome::LaunchRequested)
    );
}

#[test]
fn download_client_opens_url() {
    let mut adapter = MacClientHandoff::new(
        LaunchTarget::Executable("/Applications/CrayonCast.app".into()),
        "https://crayon.example/download".into(),
        Box::new(|_| true),
    );
    let request = make_request(HandoffAction::DownloadClient);
    assert_eq!(
        adapter.perform(&request),
        Ok(HandoffOutcome::DownloadStarted)
    );
}

#[test]
fn executor_failure_is_unavailable() {
    let mut adapter = MacClientHandoff::new(
        LaunchTarget::Executable("/nonexistent".into()),
        "https://crayon.example/download".into(),
        Box::new(|_| false),
    );
    let request = make_request(HandoffAction::LaunchClient);
    assert_eq!(adapter.perform(&request), Err(HandoffError::Unavailable));
}
