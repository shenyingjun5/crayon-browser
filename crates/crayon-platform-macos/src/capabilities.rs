//! macOS adapter capability document (PLT-02 model, PLT-M04 truth).
//!
//! Only surfaces delivered by a landed PLT-M04 slice are declared
//! available; everything else reports unavailable until its slice
//! merges.  The document is collected once and validated against the
//! schema.

use crayon_platform_capabilities::{
    ExternalClientHandoffCapabilities, LifecycleCapabilities, LocalAgentIpcCapabilities,
    LocalNetworkCapabilities, PlatformAdapterCapabilities, SecureStoreBackend,
    SecureStoreCapabilities, SupportLevel, UpdateCapabilities,
};

#[cfg(test)]
#[path = "capabilities_tests.rs"]
mod tests;

/// Builds the current macOS adapter capability document.
///
/// Truth per slice: M04a lands Keychain secure storage, M04b delivers
/// local-network observation (requires_permission per CP-M01) and
/// power/lock lifecycle events; update, UDS IPC and client handoff
/// stay unavailable until M04c..d deliver them.
#[must_use]
pub fn macos_adapter_capabilities() -> PlatformAdapterCapabilities {
    PlatformAdapterCapabilities::new(
        SecureStoreCapabilities {
            backend: SecureStoreBackend::Keychain,
            rotation: true,
        },
        LocalNetworkCapabilities {
            observation: SupportLevel::RequiresPermission,
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
            transport: crayon_platform_capabilities::AgentIpcTransport::Unavailable,
            peer_credentials: false,
            per_user_acl: false,
        },
        ExternalClientHandoffCapabilities {
            download: false,
            launch: false,
        },
    )
    .normalized()
}
