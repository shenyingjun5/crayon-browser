//! Platform-neutral product runtime composition.

pub mod cast_usecase;
pub mod content_host_runtime;
#[cfg(test)]
mod content_host_runtime_tests;
pub mod delivery;
pub mod media_host_runtime;
#[cfg(test)]
mod media_host_runtime_tests;
pub mod media_planning_runtime;
#[cfg(test)]
mod media_planning_runtime_tests;
pub mod navigation_usecase;
#[cfg(test)]
mod navigation_usecase_tests;
pub mod page_snapshot_runtime;
pub mod semantic_action_runtime;
#[cfg(test)]
mod semantic_action_runtime_tests;

use crayon_domain::{ProductIdentity, ProductIdentityError, ProductMode};
use crayon_ipc_schema::Handshake;

/// Coherent startup facts consumed by a formal browser shell.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RuntimeDescriptor {
    identity: ProductIdentity,
    handshake: Handshake,
}

impl RuntimeDescriptor {
    /// Creates the descriptor used by formal product entry points.
    pub fn formal(product_name: &'static str) -> Result<Self, ProductIdentityError> {
        let identity = ProductIdentity::new(product_name, ProductMode::Formal)?;
        Ok(Self {
            identity,
            handshake: Handshake::current(identity.mode()),
        })
    }

    #[must_use]
    pub const fn identity(self) -> ProductIdentity {
        self.identity
    }

    #[must_use]
    pub const fn handshake(self) -> Handshake {
        self.handshake
    }
}
