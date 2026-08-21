//! Platform-independent domain foundations shared by Crayon product crates.

mod capabilities;
mod config;
mod diagnostics;
mod error;
mod ids;

pub use capabilities::{
    BrowserEngineKind, LocalDiscoveryKind, PlatformCapabilities, ProtectedSurfaceKind,
    ReceiverCapabilities, SecureStoreKind,
};
pub use config::{
    CapacityConfig, ConfigError, LogLevel, LoggingConfig, NetworkConfig, ProductConfig,
    TimeoutConfig, UpdateChannel, UpdateSection, CONFIG_SCHEMA_VERSION,
};
pub use diagnostics::{
    redact_sensitive, DataClass, DiagnosticError, DiagnosticEvent, DiagnosticProducer,
    DEFAULT_QUEUE_CAPACITY, DIAGNOSTICS_SCHEMA_VERSION, MAX_ATTRIBUTES_PER_EVENT,
    MAX_ATTRIBUTE_KEY_LEN, MAX_ATTRIBUTE_VALUE_LEN, MAX_EVENT_NAME_LEN,
};
pub use error::CoreError;
pub use ids::{DeviceId, IdError, ResourceId, SessionGeneration, SessionId, TabId};

use serde::{Deserialize, Serialize};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Explicit product execution boundary.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProductMode {
    /// Shipping product mode. Legacy extraction and relay surfaces are forbidden.
    Formal,
    /// Migration-only mode used by the explicitly labelled legacy application.
    LegacyDevelopment,
}

impl ProductMode {
    /// Reports whether this mode may enter a legacy migration adapter.
    #[must_use]
    pub const fn permits_legacy_adapter(self) -> bool {
        matches!(self, Self::LegacyDevelopment)
    }
}

/// Validated identity used when composing the product runtime.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ProductIdentity {
    name: &'static str,
    mode: ProductMode,
}

impl ProductIdentity {
    /// Creates an identity. Whitespace-only names are rejected at the boundary.
    pub fn new(name: &'static str, mode: ProductMode) -> Result<Self, ProductIdentityError> {
        if name.trim().is_empty() {
            return Err(ProductIdentityError::EmptyName);
        }
        Ok(Self { name, mode })
    }

    #[must_use]
    pub const fn name(self) -> &'static str {
        self.name
    }

    #[must_use]
    pub const fn mode(self) -> ProductMode {
        self.mode
    }
}

/// Product identity validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProductIdentityError {
    EmptyName,
}

impl Display for ProductIdentityError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::EmptyName => formatter.write_str("product name must not be empty"),
        }
    }
}

impl Error for ProductIdentityError {}
