//! Windows update-flow driver over the PLT-01 closed state machine
//! (PLT-W04d).
//!
//! The adapter owns no network stack: check/download/install operations
//! are injected operations supplied by the product assembly (the real
//! update service belongs to QAR-09 packaging).  Every command still
//! passes through the frozen `UpdateState::transition` function, so
//! illegal transitions stay stable rejections.

use crayon_platform_api::update::{UpdateCommand, UpdateFlow, UpdateFlowError, UpdateState};

/// Outcome of an injected update check.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CheckOutcome {
    NoUpdate,
    UpdateAvailable,
    Failure,
}

/// One injected platform operation set.
pub struct UpdateOperations {
    /// Performs the update check.
    pub check: Box<dyn FnMut() -> CheckOutcome + Send>,
    /// Downloads the package; `true` when the download completed.
    pub download: Box<dyn FnMut() -> bool + Send>,
    /// Runs the user-consented install handoff.
    pub install: Box<dyn FnMut() -> bool + Send>,
}

impl std::fmt::Debug for UpdateOperations {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("UpdateOperations { .. }")
    }
}

/// Windows update-flow driver.
#[derive(Debug)]
pub struct WindowsUpdateFlow {
    state: UpdateState,
    operations: UpdateOperations,
}

impl WindowsUpdateFlow {
    /// Creates an idle flow with the given injected operations.
    #[must_use]
    pub fn new(operations: UpdateOperations) -> Self {
        Self {
            state: UpdateState::Idle,
            operations,
        }
    }
}

impl UpdateFlow for WindowsUpdateFlow {
    fn state(&self) -> UpdateState {
        self.state
    }

    fn dispatch(&mut self, command: UpdateCommand) -> Result<UpdateState, UpdateFlowError> {
        use UpdateCommand as C;
        // Step 1: user-command transition (illegal commands are stable
        // rejections and run no injected operation).
        let intermediate = self.state.transition(command)?;
        // Step 2: perform the injected operation and apply its closed
        // result fact from the intermediate state.
        match command {
            C::StartCheck => {
                use CheckOutcome as O;
                match (self.operations.check)() {
                    O::NoUpdate => self.commit(intermediate.transition(C::CheckSucceededNoUpdate)),
                    O::UpdateAvailable => {
                        self.commit(intermediate.transition(C::CheckSucceededUpdateAvailable))
                    }
                    O::Failure => self.commit(intermediate.transition(C::CheckFailed)),
                }
            }
            C::StartDownload => {
                let completed = (self.operations.download)();
                if completed {
                    self.commit(intermediate.transition(C::DownloadCompleted))
                } else {
                    self.commit(intermediate.transition(C::DownloadFailed))
                }
            }
            C::Install => {
                if (self.operations.install)() {
                    self.commit(Ok(intermediate))
                } else {
                    // Platform refused the consented install handoff; the
                    // flow stays ReadyToInstall and reports a stable
                    // rejection instead of pretending success.
                    self.commit(Err(UpdateFlowError::IllegalTransition {
                        from: self.state,
                        command,
                    }))
                }
            }
            // Facts and dismissals carry no operation of their own.
            _ => self.commit(Ok(intermediate)),
        }
    }
}

impl WindowsUpdateFlow {
    /// Commits a computed next state, keeping the flow unchanged on
    /// rejection (trait contract).
    fn commit(
        &mut self,
        next: Result<UpdateState, UpdateFlowError>,
    ) -> Result<UpdateState, UpdateFlowError> {
        match next {
            Ok(state) => {
                self.state = state;
                Ok(state)
            }
            Err(error) => Err(error),
        }
    }
}

#[cfg(test)]
#[path = "update_tests.rs"]
mod tests;
