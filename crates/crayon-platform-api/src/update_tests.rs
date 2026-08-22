use super::*;

fn legal_table() -> Vec<(UpdateState, UpdateCommand, UpdateState)> {
    use UpdateCommand as C;
    use UpdateState as S;
    vec![
        (S::Idle, C::StartCheck, S::Checking),
        (S::Checking, C::CheckSucceededNoUpdate, S::Idle),
        (S::Checking, C::CheckSucceededUpdateAvailable, S::Available),
        (S::Checking, C::CheckFailed, S::Failed),
        (S::Available, C::StartCheck, S::Checking),
        (S::Available, C::StartDownload, S::Downloading),
        (S::Downloading, C::DownloadProgressed, S::Downloading),
        (S::Downloading, C::DownloadCompleted, S::ReadyToInstall),
        (S::Downloading, C::DownloadFailed, S::Failed),
        (S::ReadyToInstall, C::Install, S::Idle),
        (S::Failed, C::DismissFailure, S::Idle),
        (S::Failed, C::StartCheck, S::Checking),
    ]
}

#[test]
fn legal_transitions_match_table() {
    for (from, command, expected) in legal_table() {
        assert_eq!(
            from.transition(command),
            Ok(expected),
            "{from:?} + {command:?}"
        );
    }
}

#[test]
fn illegal_transitions_are_stable_rejections() {
    use UpdateCommand as C;
    use UpdateState as S;
    let illegal: &[(UpdateState, UpdateCommand)] = &[
        (S::Idle, C::StartDownload),
        (S::Idle, C::Install),
        (S::Checking, C::StartDownload),
        (S::Available, C::Install),
        (S::Available, C::CheckSucceededUpdateAvailable),
        (S::Downloading, C::StartCheck),
        (S::ReadyToInstall, C::StartDownload),
        (S::ReadyToInstall, C::DownloadCompleted),
        (S::Failed, C::StartDownload),
    ];
    for (from, command) in illegal {
        let err = from.transition(*command).unwrap_err();
        assert_eq!(
            err,
            UpdateFlowError::IllegalTransition {
                from: *from,
                command: *command
            }
        );
    }
}

#[test]
fn full_happy_path_reaches_install() {
    use UpdateCommand as C;
    use UpdateState as S;
    let state = S::Idle
        .transition(C::StartCheck)
        .unwrap()
        .transition(C::CheckSucceededUpdateAvailable)
        .unwrap()
        .transition(C::StartDownload)
        .unwrap()
        .transition(C::DownloadProgressed)
        .unwrap()
        .transition(C::DownloadCompleted)
        .unwrap();
    assert_eq!(state, S::ReadyToInstall);
    assert_eq!(state.transition(C::Install), Ok(S::Idle));
}
