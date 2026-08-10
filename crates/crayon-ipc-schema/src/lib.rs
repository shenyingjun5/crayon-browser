//! Version negotiation primitives for browser/core communication.

use crayon_domain::ProductMode;
use std::num::NonZeroU16;

/// Non-zero IPC schema version.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct SchemaVersion(NonZeroU16);

impl SchemaVersion {
    pub const CURRENT: Self = Self(NonZeroU16::MIN);

    #[must_use]
    pub const fn new(value: NonZeroU16) -> Self {
        Self(value)
    }

    #[must_use]
    pub const fn get(self) -> u16 {
        self.0.get()
    }

    /// Uses exact-version negotiation until the v1 compatibility window is frozen.
    #[must_use]
    pub const fn is_supported_by(self, peer: Self) -> bool {
        self.get() == peer.get()
    }
}

/// Minimal startup negotiation data shared by browser and core.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Handshake {
    schema_version: SchemaVersion,
    product_mode: ProductMode,
}

impl Handshake {
    #[must_use]
    pub const fn current(product_mode: ProductMode) -> Self {
        Self {
            schema_version: SchemaVersion::CURRENT,
            product_mode,
        }
    }

    #[must_use]
    pub const fn schema_version(self) -> SchemaVersion {
        self.schema_version
    }

    #[must_use]
    pub const fn product_mode(self) -> ProductMode {
        self.product_mode
    }
}
