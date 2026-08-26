//! M04d update flow tests: pure state transitions on the frozen PLT-01
//! state machine.  The caller drives operations and reports outcomes.

use super::*;

#[test]
fn happy_path_check_to_install() {
    let mut flow = MacUpdateFlow::new();
    assert_eq!(flow.state(), UpdateState::Idle);
    assert_eq!(
        flow.dispatch(UpdateCommand::StartCheck).unwrap(),
        UpdateState::Checking
    );
    assert_eq!(
        flow.dispatch(UpdateCommand::CheckSucceededUpdateAvailable)
            .unwrap(),
        UpdateState::Available
    );
    assert_eq!(
        flow.dispatch(UpdateCommand::StartDownload).unwrap(),
        UpdateState::Downloading
    );
    assert_eq!(
        flow.dispatch(UpdateCommand::DownloadCompleted).unwrap(),
        UpdateState::ReadyToInstall
    );
    assert_eq!(
        flow.dispatch(UpdateCommand::Install).unwrap(),
        UpdateState::Idle
    );
}

#[test]
fn check_failure_goes_to_failed() {
    let mut flow = MacUpdateFlow::new();
    flow.dispatch(UpdateCommand::StartCheck).unwrap();
    assert_eq!(
        flow.dispatch(UpdateCommand::CheckFailed).unwrap(),
        UpdateState::Failed
    );
}

#[test]
fn download_failure_goes_to_failed() {
    let mut flow = MacUpdateFlow::new();
    flow.dispatch(UpdateCommand::StartCheck).unwrap();
    flow.dispatch(UpdateCommand::CheckSucceededUpdateAvailable)
        .unwrap();
    flow.dispatch(UpdateCommand::StartDownload).unwrap();
    assert_eq!(
        flow.dispatch(UpdateCommand::DownloadFailed).unwrap(),
        UpdateState::Failed
    );
}

#[test]
fn failed_can_restart_check() {
    let mut flow = MacUpdateFlow::new();
    flow.dispatch(UpdateCommand::StartCheck).unwrap();
    flow.dispatch(UpdateCommand::CheckFailed).unwrap();
    assert_eq!(
        flow.dispatch(UpdateCommand::StartCheck).unwrap(),
        UpdateState::Checking
    );
}

#[test]
fn illegal_transitions_are_stable_rejections() {
    let mut flow = MacUpdateFlow::new();
    // Idle → Install is illegal.
    assert!(flow.dispatch(UpdateCommand::Install).is_err());
    // Idle → DownloadCompleted is illegal.
    assert!(flow.dispatch(UpdateCommand::DownloadCompleted).is_err());
    // State unchanged after rejection.
    assert_eq!(flow.state(), UpdateState::Idle);
}
