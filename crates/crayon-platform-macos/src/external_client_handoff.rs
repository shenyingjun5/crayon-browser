//! Standalone cast-client handoff over macOS `open` (PLT-M04d).
//!
//! The adapter performs exactly the confirmed request: launching the
//! installed client or opening the official download page in the
//! default browser via the `open` command.  It never claims a casting
//! session started and carries no page data.  The launch path and
//! download URL are injected by the product assembly.

use crayon_platform_api::external_client_handoff::{
    ExternalClientHandoff, HandoffAction, HandoffError, HandoffOutcome, HandoffRequest,
};

#[cfg(test)]
#[path = "external_client_handoff_tests.rs"]
mod tests;

/// What the executor was asked to open.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum LaunchTarget {
    /// An executable path on this machine.
    Executable(String),
    /// An https URL handed to the default browser.
    Url(String),
}

/// Injected OS executor: runs `open <target>` on macOS.  Returns true
/// when the launch succeeded.
pub type OpenExecutor = Box<dyn Fn(&LaunchTarget) -> bool + Send>;

/// macOS external-client handoff adapter.
pub struct MacClientHandoff {
    /// The installed client's launch target (injected by assembly).
    client_target: LaunchTarget,
    /// The official download page URL (injected by assembly).
    download_url: String,
    executor: OpenExecutor,
}

impl MacClientHandoff {
    /// Creates a handoff adapter with injected targets and executor.
    pub fn new(client_target: LaunchTarget, download_url: String, executor: OpenExecutor) -> Self {
        Self {
            client_target,
            download_url,
            executor,
        }
    }

    /// Resolves the launch target for a given action.
    fn target_for(&self, action: HandoffAction) -> LaunchTarget {
        match action {
            HandoffAction::LaunchClient => self.client_target.clone(),
            HandoffAction::DownloadClient => LaunchTarget::Url(self.download_url.clone()),
        }
    }
}

impl ExternalClientHandoff for MacClientHandoff {
    fn perform(&mut self, request: &HandoffRequest) -> Result<HandoffOutcome, HandoffError> {
        let target = self.target_for(request.action());
        let launched = (self.executor)(&target);
        if !launched {
            return Err(HandoffError::Unavailable);
        }
        match request.action() {
            HandoffAction::LaunchClient => Ok(HandoffOutcome::LaunchRequested),
            HandoffAction::DownloadClient => Ok(HandoffOutcome::DownloadStarted),
        }
    }
}
