//! macOS update-flow driver over the PLT-01 closed state machine
//! (PLT-M04d).
//!
//! The adapter owns no network stack: the caller drives check/download/
//! install operations and reports outcomes as commands.  Every command
//! passes through the frozen `UpdateState::transition` function.

use crayon_platform_api::update::{UpdateCommand, UpdateFlow, UpdateFlowError, UpdateState};

#[cfg(test)]
#[path = "update_tests.rs"]
mod tests;

/// macOS update flow driver (pure state transition, no side effects).
pub struct MacUpdateFlow {
    state: UpdateState,
}

impl MacUpdateFlow {
    pub fn new() -> Self {
        Self {
            state: UpdateState::Idle,
        }
    }
}

impl Default for MacUpdateFlow {
    fn default() -> Self {
        Self::new()
    }
}

impl UpdateFlow for MacUpdateFlow {
    fn state(&self) -> UpdateState {
        self.state
    }

    fn dispatch(&mut self, command: UpdateCommand) -> Result<UpdateState, UpdateFlowError> {
        self.state = self.state.transition(command)?;
        Ok(self.state)
    }
}
