//! Read-only platform adapter capability model (PLT-02).
//!
//! Capabilities are collected once at startup by the platform adapter and
//! are read-only afterwards.  Shared policy code branches on these declared
//! capabilities, never on OS or device-model checks.  The wire form carries
//! no user identity, device identifiers, URLs or version fingerprint
//! strings — only closed enums and booleans.

use serde::{Deserialize, Serialize};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Wire schema version carried by the aggregate capability document.
pub const ADAPTER_CAPABILITIES_SCHEMA_VERSION: u32 = 1;

/// Closed support level for one capability surface.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SupportLevel {
    Unavailable,
    Available,
    /// The facility exists but requires a user-granted OS permission
    /// (e.g. macOS local network).
    RequiresPermission,
}

impl SupportLevel {
    /// Reports whether the surface can be used without further permission
    /// flow.
    #[must_use]
    pub const fn is_usable(self) -> bool {
        matches!(self, Self::Available)
    }
}

/// OS secure-storage backend.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SecureStoreBackend {
    Dpapi,
    Keychain,
    Unavailable,
}

/// Secure storage capabilities.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SecureStoreCapabilities {
    pub backend: SecureStoreBackend,
    /// Key rotation without data loss is supported.
    pub rotation: bool,
}

/// Local network observation capabilities.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LocalNetworkCapabilities {
    pub observation: SupportLevel,
    /// Asynchronous interface/route change events are supported.
    pub change_events: bool,
}

/// Power/session lifecycle capabilities.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LifecycleCapabilities {
    /// Suspend/resume events are observable.
    pub power_events: bool,
    /// Screen lock/unlock events are observable.
    pub lock_events: bool,
}

/// Update facility capabilities.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateCapabilities {
    pub service: SupportLevel,
    /// Downloaded packages are signature-verified by the platform flow.
    pub signed_packages: bool,
}

/// Local agent IPC transport.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AgentIpcTransport {
    NamedPipe,
    UnixDomainSocket,
    Unavailable,
}

/// Current-user local agent IPC capabilities (AG-012 gate inputs).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LocalAgentIpcCapabilities {
    pub transport: AgentIpcTransport,
    /// The transport can verify the peer's OS user identity.
    pub peer_credentials: bool,
    /// The endpoint ACL can be restricted to the current user.
    pub per_user_acl: bool,
}

/// External cast-client handoff capabilities.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ExternalClientHandoffCapabilities {
    /// The platform can download the standalone client.
    pub download: bool,
    /// The platform can launch the installed client.
    pub launch: bool,
}

/// Aggregate read-only capability set for the six PLT-01 surfaces.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PlatformAdapterCapabilities {
    schema: u32,
    pub secure_store: SecureStoreCapabilities,
    pub local_network: LocalNetworkCapabilities,
    pub lifecycle: LifecycleCapabilities,
    pub update: UpdateCapabilities,
    pub local_agent_ipc: LocalAgentIpcCapabilities,
    pub external_client_handoff: ExternalClientHandoffCapabilities,
}

/// Capability validation failure.  Variants are stable and carry no data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CapabilityError {
    /// The schema version is not supported.
    UnsupportedSchema,
    /// A contradictory combination was found (e.g. peer credentials
    /// without a transport).
    Inconsistent,
}

impl Display for CapabilityError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::UnsupportedSchema => "adapter capabilities schema version is not supported",
            Self::Inconsistent => "adapter capabilities contain a contradictory combination",
        };
        formatter.write_str(message)
    }
}

impl Error for CapabilityError {}

impl PlatformAdapterCapabilities {
    /// Creates an aggregate with the current schema version.
    #[must_use]
    pub const fn new(
        secure_store: SecureStoreCapabilities,
        local_network: LocalNetworkCapabilities,
        lifecycle: LifecycleCapabilities,
        update: UpdateCapabilities,
        local_agent_ipc: LocalAgentIpcCapabilities,
        external_client_handoff: ExternalClientHandoffCapabilities,
    ) -> Self {
        Self {
            schema: ADAPTER_CAPABILITIES_SCHEMA_VERSION,
            secure_store,
            local_network,
            lifecycle,
            update,
            local_agent_ipc,
            external_client_handoff,
        }
    }

    #[must_use]
    pub const fn schema(&self) -> u32 {
        self.schema
    }

    /// Folds contradictory combinations toward consistency: without a
    /// transport, peer-credential and ACL capabilities are meaningless and
    /// fold to false.
    #[must_use]
    pub fn normalized(mut self) -> Self {
        if self.local_agent_ipc.transport == AgentIpcTransport::Unavailable {
            self.local_agent_ipc.peer_credentials = false;
            self.local_agent_ipc.per_user_acl = false;
        }
        self
    }

    /// Re-checks a decoded document against the schema invariants.
    pub fn validate(&self) -> Result<(), CapabilityError> {
        if self.schema != ADAPTER_CAPABILITIES_SCHEMA_VERSION {
            return Err(CapabilityError::UnsupportedSchema);
        }
        if self.local_agent_ipc.transport == AgentIpcTransport::Unavailable
            && (self.local_agent_ipc.peer_credentials || self.local_agent_ipc.per_user_acl)
        {
            return Err(CapabilityError::Inconsistent);
        }
        Ok(())
    }
}
