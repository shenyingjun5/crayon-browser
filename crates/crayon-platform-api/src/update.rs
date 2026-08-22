//! Software update contract: closed state machine plus the adapter trait.

use std::error::Error;
use std::fmt::{Display, Formatter};

/// Closed states of the update flow.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UpdateState {
    Idle,
    Checking,
    /// An update exists and can be downloaded.
    Available,
    Downloading,
    /// Downloaded and verified; installing requires explicit user consent.
    ReadyToInstall,
    Failed,
}

/// Closed commands driving the update flow.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UpdateCommand {
    StartCheck,
    CheckSucceededNoUpdate,
    CheckSucceededUpdateAvailable,
    CheckFailed,
    StartDownload,
    DownloadProgressed,
    DownloadCompleted,
    DownloadFailed,
    /// User-consented install handoff.
    Install,
    DismissFailure,
}

/// Update-flow transition failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum UpdateFlowError {
    /// The command is not legal in the current state.
    IllegalTransition {
        from: UpdateState,
        command: UpdateCommand,
    },
}

impl Display for UpdateFlowError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::IllegalTransition { from, command } => write!(
                formatter,
                "illegal update transition from {from:?} via {command:?}"
            ),
        }
    }
}

impl Error for UpdateFlowError {}

impl UpdateState {
    /// Total, deterministic transition function: every (state, command)
    /// pair either advances or is a stable rejection.
    pub fn transition(self, command: UpdateCommand) -> Result<Self, UpdateFlowError> {
        use UpdateCommand as C;
        use UpdateState as S;
        let illegal = || UpdateFlowError::IllegalTransition {
            from: self,
            command,
        };
        match (self, command) {
            (S::Idle, C::StartCheck)
            | (S::Available, C::StartCheck)
            | (S::Failed, C::StartCheck) => Ok(S::Checking),
            (S::Checking, C::CheckSucceededNoUpdate) => Ok(S::Idle),
            (S::Checking, C::CheckSucceededUpdateAvailable) => Ok(S::Available),
            (S::Checking, C::CheckFailed) => Ok(S::Failed),
            (S::Available, C::StartDownload) => Ok(S::Downloading),
            (S::Downloading, C::DownloadProgressed) => Ok(S::Downloading),
            (S::Downloading, C::DownloadCompleted) => Ok(S::ReadyToInstall),
            (S::Downloading, C::DownloadFailed) => Ok(S::Failed),
            (S::ReadyToInstall, C::Install) => Ok(S::Idle),
            (S::Failed, C::DismissFailure) => Ok(S::Idle),
            _ => Err(illegal()),
        }
    }
}

/// Platform update facility driven through the closed state machine.
pub trait UpdateFlow: Send {
    /// Current flow state.
    fn state(&self) -> UpdateState;

    /// Dispatches a command; illegal transitions are stable rejections and
    /// leave the state unchanged.
    fn dispatch(&mut self, command: UpdateCommand) -> Result<UpdateState, UpdateFlowError>;
}

#[cfg(test)]
#[path = "update_tests.rs"]
mod tests;
