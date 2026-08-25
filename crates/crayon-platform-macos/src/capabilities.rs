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
/// Truth per slice: M04a lands Keychain secure storage only; local
/// network, lifecycle, update, UDS IPC and client handoff stay
/// unavailable until slices M04b..d deliver them.
#[must_use]
pub fn macos_adapter_capabilities() -> PlatformAdapterCapabilities {
    PlatformAdapterCapabilities::new(
        SecureStoreCapabilities {
            backend: SecureStoreBackend::Keychain,
            rotation: true,
        },
        LocalNetworkCapabilities {
            observation: SupportLevel::Unavailable,
            change_events: false,
        },
        LifecycleCapabilities {
            power_events: false,
            lock_events: false,
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
