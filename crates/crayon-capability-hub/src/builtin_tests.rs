//! HUB-02 builtin catalog tests: HB-002 authoritative registration, no
//! duplicate schema and no hidden strong capability.

use super::*;
use crayon_domain::{CapabilitySource, LifecycleState};

#[test]
fn full_catalog_registers_through_the_public_path() {
    let registry = builtin_registry();
    assert_eq!(registry.len(), BUILTIN_IDS.len());
    for id in BUILTIN_IDS {
        let view = registry.find(id).expect("builtin registered");
        assert_eq!(view.descriptor.id, id);
        assert_eq!(view.state, LifecycleState::Active);
        assert_eq!(view.descriptor.version, BUILTIN_CATALOG_VERSION);
    }
}

#[test]
fn every_descriptor_is_builtin_system_and_schema_valid() {
    for descriptor in builtin_descriptors() {
        descriptor.validate().expect("builtin schema valid");
        assert_eq!(descriptor.source, CapabilitySource::Builtin);
        assert_eq!(descriptor.trust, crayon_domain::TrustLevel::System);
        assert!(
            descriptor.id.starts_with("builtin."),
            "id {id} must use the builtin namespace",
            id = descriptor.id
        );
        assert!(descriptor.summary.len() <= crayon_domain::MAX_CAPABILITY_SUMMARY_LEN);
    }
}

/// Data scopes are pinned per domain: navigation touches no user data,
/// content reads the bounded page surface, cast goes through cast gates,
/// handoff keeps everything local.
#[test]
fn data_scopes_match_the_frozen_domain_mapping() {
    let expected = [
        ("builtin.browser", crayon_domain::DataScope::LocalOnly),
        ("builtin.content", crayon_domain::DataScope::PageContent),
        ("builtin.cast", crayon_domain::DataScope::CastControl),
        ("builtin.handoff", crayon_domain::DataScope::LocalOnly),
    ];
    let registry = builtin_registry();
    for (id, scope) in expected {
        assert_eq!(
            registry.find(id).expect("registered").descriptor.data_scope,
            scope,
            "data scope of {id} diverged"
        );
    }
}

/// The id list is closed: no extra entry may ship silently.
#[test]
fn catalog_ids_are_exactly_the_frozen_set() {
    let descriptors = builtin_descriptors();
    let ids: Vec<&str> = descriptors.iter().map(|d| d.id.as_str()).collect();
    assert_eq!(ids, BUILTIN_IDS.to_vec());
}

/// No hidden strong capability: no builtin id hits the AGT permanent
/// deny vocabulary (raw CDP/WebDriver/JS/cookie/file/network surfaces).
#[test]
fn no_builtin_id_hits_the_permanent_deny_list() {
    for id in BUILTIN_IDS {
        assert!(
            !crayon_agent_gateway::registry::is_permanently_denied(id),
            "builtin id {id} must not hit the permanent deny list"
        );
    }
}

#[test]
fn builtins_cannot_be_overridden_or_duplicated() {
    let mut registry = builtin_registry();
    // Same version again: stable duplicate rejection.
    assert_eq!(
        register_builtins(&mut registry),
        Err(RegistryError::DuplicateRegistration)
    );
    // Lower-precedence sources can never take a builtin slot, at any
    // version.
    type DescriptorMaker = fn(&str) -> CapabilityDescriptor;
    let interlopers: [(&str, DescriptorMaker); 2] = [
        ("personal skill", personal_descriptor),
        ("partner package", partner_descriptor),
    ];
    for (label, make) in interlopers {
        for id in BUILTIN_IDS {
            assert_eq!(
                registry.register(make(id)),
                Err(RegistryError::Conflict),
                "{label} must not override builtin {id}"
            );
        }
    }
    // The original registrations are untouched after all rejections.
    assert_eq!(registry.len(), BUILTIN_IDS.len());
    for id in BUILTIN_IDS {
        let view = registry.find(id).expect("builtin intact");
        assert_eq!(view.descriptor.source, CapabilitySource::Builtin);
        assert_eq!(view.descriptor.version, BUILTIN_CATALOG_VERSION);
    }
}

fn personal_descriptor(id: &str) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: "9.9.9".to_owned(),
        source: CapabilitySource::PersonalSkill,
        trust: crayon_domain::TrustLevel::UserApproved,
        data_scope: crayon_domain::DataScope::LocalOnly,
        summary: String::new(),
    }
}

fn partner_descriptor(id: &str) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: "9.9.9".to_owned(),
        source: CapabilitySource::Partner,
        trust: crayon_domain::TrustLevel::Untrusted,
        data_scope: crayon_domain::DataScope::ExternalEndpoint,
        summary: String::new(),
    }
}

fn golden_path() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests")
        .join("capability_registry_builtin_v1.txt")
}

#[test]
fn builtin_snapshot_matches_frozen_golden() {
    let actual = builtin_registry().snapshot();
    let golden = std::fs::read_to_string(golden_path()).expect("builtin golden must exist");
    assert_eq!(actual, golden);
}
