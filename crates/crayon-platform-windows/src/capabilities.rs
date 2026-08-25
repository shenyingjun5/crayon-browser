//! Windows adapter capability document (PLT-02 model, PLT-W04 truth).
//!
//! Only surfaces delivered by a landed PLT-W04 slice are declared
//! available; everything else reports unavailable until its slice merges.
//! The document is collected once and validated against the schema.

use crayon_platform_capabilities::{
    ExternalClientHandoffCapabilities, LifecycleCapabilities, LocalAgentIpcCapabilities,
    LocalNetworkCapabilities, PlatformAdapterCapabilities, SecureStoreBackend,
    SecureStoreCapabilities, SupportLevel, UpdateCapabilities,
};

#[cfg(test)]
#[path = "capabilities_tests.rs"]
mod tests;

/// Builds the current Windows adapter capability document.
///
/// Truth per slice: W04a lands DPAPI secure storage only; local network,
/// lifecycle, update, named-pipe IPC and client handoff stay unavailable
/// until slices W04b..d deliver them.
#[must_use]
pub fn windows_adapter_capabilities() -> PlatformAdapterCapabilities {
    PlatformAdapterCapabilities::new(
        SecureStoreCapabilities {
            backend: SecureStoreBackend::Dpapi,
            rotation: false,
        },
        LocalNetworkCapabilities {
            observation: SupportLevel::Available,
            change_events: true,
        },
        LifecycleCapabilities {
            power_events: true,
            lock_events: true,
        },
        UpdateCapabilities {
            service: SupportLevel::Unavailable,
            signed_packages: false,
        },
        LocalAgentIpcCapabilities {
            transport: crayon_platform_capabilities::AgentIpcTransport::NamedPipe,
            peer_credentials: true,
            per_user_acl: true,
        },
        ExternalClientHandoffCapabilities {
            download: false,
            launch: false,
        },
    )
    .normalized()
}
