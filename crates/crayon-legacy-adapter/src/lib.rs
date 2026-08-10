//! Explicit, non-shipping access boundary for legacy migration code.

use crayon_domain::ProductMode;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Proof that a caller explicitly selected legacy development mode.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LegacyBoundary {
    mode: ProductMode,
}

impl LegacyBoundary {
    /// Rejects formal product callers instead of silently enabling legacy behavior.
    pub fn enter(mode: ProductMode) -> Result<Self, LegacyBoundaryError> {
        if !mode.permits_legacy_adapter() {
            return Err(LegacyBoundaryError::FormalProductForbidden);
        }
        Ok(Self { mode })
    }

    #[must_use]
    pub const fn mode(self) -> ProductMode {
        self.mode
    }
}

/// Failure to cross the explicit migration boundary.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum LegacyBoundaryError {
    FormalProductForbidden,
}

impl Display for LegacyBoundaryError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::FormalProductForbidden => {
                formatter.write_str("formal product mode cannot enter the legacy adapter")
            }
        }
    }
}

impl Error for LegacyBoundaryError {}
