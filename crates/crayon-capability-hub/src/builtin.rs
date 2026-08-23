//! Authoritative built-in capability catalog (HUB-02).
//!
//! The four frozen v1 entries — browser, content, cast and handoff —
//! are compiled in from this module as the single source of truth.  They
//! enter the registry through the ordinary public registration path only:
//! no bypass injection, no hidden strong capability, no partner or skill
//! surface.  Router, policy and fallback belong to later HUB tasks.

use crate::registry::{CapabilityRegistry, RegistryError};
use crayon_domain::{CapabilityDescriptor, CapabilitySource, DataScope, TrustLevel};

/// Frozen catalog version shared by every built-in descriptor.
pub const BUILTIN_CATALOG_VERSION: &str = "1.0.0";

/// Frozen v1 built-in ids; the closed set locked by the golden tests.
pub const BUILTIN_IDS: [&str; 4] = [
    "builtin.browser",
    "builtin.content",
    "builtin.cast",
    "builtin.handoff",
];

fn descriptor(id: &str, scope: DataScope, summary: &str) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: BUILTIN_CATALOG_VERSION.to_owned(),
        source: CapabilitySource::Builtin,
        trust: TrustLevel::System,
        data_scope: scope,
        summary: summary.to_owned(),
    }
}

/// The authoritative v1 built-in descriptors, in frozen `BUILTIN_IDS`
/// order.
#[must_use]
pub fn builtin_descriptors() -> Vec<CapabilityDescriptor> {
    vec![
        descriptor(
            "builtin.browser",
            DataScope::LocalOnly,
            "Controlled browser navigation and tab operations",
        ),
        descriptor(
            "builtin.content",
            DataScope::PageContent,
            "Bounded current-page content extraction and deterministic Markdown",
        ),
        descriptor(
            "builtin.cast",
            DataScope::CastControl,
            "LAN cast session selection and playback control through the normal cast gates",
        ),
        descriptor(
            "builtin.handoff",
            DataScope::LocalOnly,
            "Pause and hand the task to the user or suggest the external cast client",
        ),
    ]
}

/// Registers every built-in descriptor into `registry` through the
/// public registration path.  Strict: any rejection aborts with the
/// registry left in a consistent state (already-registered builtins stay
/// registered).
pub fn register_builtins(registry: &mut CapabilityRegistry) -> Result<(), RegistryError> {
    for builtin in builtin_descriptors() {
        registry.register(builtin)?;
    }
    Ok(())
}

/// A registry preloaded with the authoritative built-in capabilities.
#[must_use]
pub fn builtin_registry() -> CapabilityRegistry {
    let mut registry = CapabilityRegistry::new();
    register_builtins(&mut registry).expect("frozen builtin catalog must register");
    registry
}

#[cfg(test)]
#[path = "builtin_tests.rs"]
mod tests;
