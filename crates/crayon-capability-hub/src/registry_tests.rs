//! HUB-01 capability registry behavior tests: registration/precedence
//! matrix, revocation terminality, lifecycle transitions, snapshot golden
//! and storm invariants.

use super::*;
use crayon_domain::{CapabilitySource, DataScope, TrustLevel};

const SNAPSHOT_SCENARIO_IDS: usize = 5;

fn descriptor(
    id: &str,
    version: &str,
    source: CapabilitySource,
    trust: TrustLevel,
    scope: DataScope,
) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: version.to_owned(),
        source,
        trust,
        data_scope: scope,
        summary: format!("{id} {version}"),
    }
}

fn builtin(id: &str, version: &str) -> CapabilityDescriptor {
    descriptor(
        id,
        version,
        CapabilitySource::Builtin,
        TrustLevel::System,
        DataScope::PageContent,
    )
}

fn personal(id: &str, version: &str) -> CapabilityDescriptor {
    descriptor(
        id,
        version,
        CapabilitySource::PersonalSkill,
        TrustLevel::UserApproved,
        DataScope::LocalOnly,
    )
}

fn partner(id: &str, version: &str) -> CapabilityDescriptor {
    descriptor(
        id,
        version,
        CapabilitySource::Partner,
        TrustLevel::Untrusted,
        DataScope::ExternalEndpoint,
    )
}

fn golden_path() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests")
        .join("capability_registry_snapshot.txt")
}

/// Builds the fixed mixed-source/lifecycle scenario locked by the golden.
fn scenario_registry() -> CapabilityRegistry {
    let mut registry = CapabilityRegistry::new();
    registry
        .register(builtin("browser.content", "1.0.0"))
        .expect("builtin registers");
    registry
        .register(descriptor(
            "browser.cast",
            "2.1.0",
            CapabilitySource::Builtin,
            TrustLevel::UserApproved,
            DataScope::CastControl,
        ))
        .expect("cast registers");
    registry
        .register(personal("skill.export", "1.0.0"))
        .expect("personal skill registers");
    registry
        .register(partner("partner.notes", "0.9.0"))
        .expect("partner registers");
    registry
        .register(descriptor(
            "legacy.tool",
            "1.0.0",
            CapabilitySource::PersonalSkill,
            TrustLevel::Untrusted,
            DataScope::LocalOnly,
        ))
        .expect("legacy tool registers");
    registry.revoke("legacy.tool", "1.0.0").expect("revoke");
    registry
        .set_enabled("browser.cast", "2.1.0", false)
        .expect("disable");
    assert_eq!(registry.len(), SNAPSHOT_SCENARIO_IDS);
    registry
}

#[test]
fn snapshot_matches_frozen_golden() {
    let registry = scenario_registry();
    let actual = registry.snapshot();
    let golden = std::fs::read_to_string(golden_path()).expect("snapshot golden must exist");
    assert_eq!(actual, golden);
    // Determinism: rebuilding the same scenario yields byte-identical output.
    assert_eq!(scenario_registry().snapshot(), actual);
}

#[test]
fn first_registration_wins_and_duplicate_is_rejected() {
    let mut registry = CapabilityRegistry::new();
    assert_eq!(registry.register(builtin("cap.a", "1.0.0")), Ok(()));
    assert_eq!(
        registry.register(builtin("cap.a", "1.0.0")),
        Err(RegistryError::DuplicateRegistration)
    );
    assert_eq!(
        registry.register(partner("cap.a", "1.0.0")),
        Err(RegistryError::Conflict)
    );
    let view = registry
        .find("cap.a")
        .expect("registration survives rejections");
    assert_eq!(view.descriptor.version, "1.0.0");
    assert_eq!(view.state, LifecycleState::Active);
    assert_eq!(registry.len(), 1);
}

/// Replacement is allowed exactly when the incoming source precedence is
/// `>=` the current one AND the version differs; every cell of the
/// 3x3 matrix is pinned here.
#[test]
fn replacement_precedence_matrix() {
    let sources = [
        (
            "partner",
            CapabilitySource::Partner,
            partner as fn(&str, &str) -> CapabilityDescriptor,
        ),
        ("personal", CapabilitySource::PersonalSkill, personal),
        ("builtin", CapabilitySource::Builtin, builtin),
    ];
    for (current_name, current_source, current_make) in sources {
        for (incoming_name, incoming_source, incoming_make) in sources {
            let id = "matrix.cap";
            let mut registry = CapabilityRegistry::new();
            registry
                .register(current_make(id, "1.0.0"))
                .expect("current registers");
            let outcome = registry.register(incoming_make(id, "2.0.0"));
            if incoming_source.precedence() >= current_source.precedence() {
                assert_eq!(
                    outcome,
                    Ok(()),
                    "{incoming_name} over {current_name} must be allowed"
                );
                let view = registry.find(id).expect("replacement stored");
                assert_eq!(view.descriptor.version, "2.0.0");
                assert_eq!(view.state, LifecycleState::Active);
            } else {
                assert_eq!(
                    outcome,
                    Err(RegistryError::Conflict),
                    "{incoming_name} over {current_name} must conflict"
                );
                let view = registry.find(id).expect("current survives conflict");
                assert_eq!(view.descriptor.version, "1.0.0");
            }
        }
    }
}

#[test]
fn equal_precedence_same_version_is_duplicate_not_override() {
    let mut registry = CapabilityRegistry::new();
    registry
        .register(builtin("cap.b", "1.0.0"))
        .expect("registers");
    assert_eq!(
        registry.register(builtin("cap.b", "1.0.0")),
        Err(RegistryError::DuplicateRegistration)
    );
    // Same version with mutated fields is still the same id+version.
    let mut forged = builtin("cap.b", "1.0.0");
    forged.summary = "forged".to_owned();
    assert_eq!(
        registry.register(forged),
        Err(RegistryError::DuplicateRegistration)
    );
    let view = registry.find("cap.b").expect("original intact");
    assert_eq!(view.descriptor.summary, "cap.b 1.0.0");
}

#[test]
fn revoke_takes_effect_immediately_and_is_terminal() {
    let mut registry = CapabilityRegistry::new();
    registry
        .register(personal("cap.c", "1.0.0"))
        .expect("registers");
    registry.revoke("cap.c", "1.0.0").expect("first revoke");
    // Idempotent repeat, both on the live pair and the archived pair.
    assert_eq!(registry.revoke("cap.c", "1.0.0"), Ok(()));
    let view = registry.find("cap.c").expect("record kept");
    assert_eq!(view.state, LifecycleState::Revoked);
    assert!(registry
        .snapshot()
        .contains("cap.c|1.0.0|personal_skill|user_approved|local_only|revoked"));
    // Terminality: the exact pair can never register again.
    assert_eq!(
        registry.register(personal("cap.c", "1.0.0")),
        Err(RegistryError::VersionRevoked)
    );
    // Unknown ids and versions are stable rejections.
    assert_eq!(
        registry.revoke("missing.cap", "1.0.0"),
        Err(RegistryError::RegistrationUnknown)
    );
    assert_eq!(
        registry.revoke("cap.c", "9.9.9"),
        Err(RegistryError::RegistrationUnknown)
    );
}

#[test]
fn new_version_registers_over_revoked_current_and_archives_it() {
    let mut registry = CapabilityRegistry::new();
    registry
        .register(builtin("cap.d", "1.0.0"))
        .expect("registers");
    registry.revoke("cap.d", "1.0.0").expect("revoked");
    registry
        .register(builtin("cap.d", "2.0.0"))
        .expect("v2 replaces revoked v1");
    let view = registry.find("cap.d").expect("current");
    assert_eq!(view.descriptor.version, "2.0.0");
    assert_eq!(view.state, LifecycleState::Active);
    // The archived v1 stays revoked forever, visible through find_version.
    let archived = registry
        .find_version("cap.d", "1.0.0")
        .expect("archived view");
    assert_eq!(archived.state, LifecycleState::Revoked);
    assert_eq!(registry.find_version("cap.d", "unknown"), None);
    // And it can never come back, even though v2 is now current.
    assert_eq!(
        registry.register(builtin("cap.d", "1.0.0")),
        Err(RegistryError::VersionRevoked)
    );
}

#[test]
fn lifecycle_transitions_are_bound_to_the_live_version() {
    let mut registry = CapabilityRegistry::new();
    registry
        .register(builtin("cap.e", "1.0.0"))
        .expect("registers");
    // Idempotent no-op transitions.
    assert_eq!(registry.set_enabled("cap.e", "1.0.0", true), Ok(()));
    assert_eq!(registry.set_enabled("cap.e", "1.0.0", false), Ok(()));
    assert_eq!(registry.set_enabled("cap.e", "1.0.0", false), Ok(()));
    assert_eq!(
        registry.find("cap.e").map(|view| view.state),
        Some(LifecycleState::Disabled)
    );
    assert_eq!(registry.set_enabled("cap.e", "1.0.0", true), Ok(()));
    assert_eq!(
        registry.find("cap.e").map(|view| view.state),
        Some(LifecycleState::Active)
    );
    // Stale version binding fails instead of acting on a replaced record.
    registry
        .register(builtin("cap.e", "2.0.0"))
        .expect("replaced");
    assert_eq!(
        registry.set_enabled("cap.e", "1.0.0", false),
        Err(RegistryError::RegistrationUnknown)
    );
    assert_eq!(
        registry.set_enabled("missing.cap", "1.0.0", false),
        Err(RegistryError::RegistrationUnknown)
    );
    // Revoked is terminal.
    registry.revoke("cap.e", "2.0.0").expect("revoked");
    assert_eq!(
        registry.set_enabled("cap.e", "2.0.0", true),
        Err(RegistryError::LifecycleTerminal)
    );
}

#[test]
fn invalid_descriptors_are_stable_rejections() {
    let mut registry = CapabilityRegistry::new();
    assert_eq!(
        registry.register(descriptor(
            "Bad Id",
            "1.0.0",
            CapabilitySource::Builtin,
            TrustLevel::System,
            DataScope::LocalOnly,
        )),
        Err(RegistryError::InvalidDescriptor)
    );
    // Partner packages can never claim system trust (schema level).
    assert_eq!(
        registry.register(descriptor(
            "partner.cap",
            "1.0.0",
            CapabilitySource::Partner,
            TrustLevel::System,
            DataScope::ExternalEndpoint,
        )),
        Err(RegistryError::InvalidDescriptor)
    );
    let mut long_summary = builtin("cap.f", "1.0.0");
    long_summary.summary = "s".repeat(crayon_domain::MAX_CAPABILITY_SUMMARY_LEN + 1);
    assert_eq!(
        registry.register(long_summary),
        Err(RegistryError::InvalidDescriptor)
    );
    assert!(registry.is_empty(), "rejected descriptors leave no trace");
}

#[test]
fn capacity_bound_is_enforced() {
    let mut registry = CapabilityRegistry::new();
    for index in 0..MAX_REGISTRATIONS {
        assert_eq!(
            registry.register(builtin(&format!("cap.{index:03}"), "1.0.0")),
            Ok(())
        );
    }
    assert_eq!(
        registry.register(builtin("cap.overflow", "1.0.0")),
        Err(RegistryError::Capacity)
    );
    assert_eq!(registry.len(), MAX_REGISTRATIONS);
}

#[test]
fn revocation_history_fail_closed_at_bound() {
    let mut registry = CapabilityRegistry::new();
    registry
        .register(builtin("cap.g", "v000"))
        .expect("registers");
    for index in 1..=MAX_REVOKED_HISTORY_PER_ID {
        let previous = format!("v{:03}", index - 1);
        let next = format!("v{index:03}");
        registry
            .revoke("cap.g", &previous)
            .expect("revoke live version");
        registry
            .register(builtin("cap.g", &next))
            .expect("next version replaces revoked one");
    }
    // History is full: another revoke+replace cycle must fail closed on
    // the registration side while revocation itself always succeeds.
    registry
        .revoke("cap.g", &format!("v{MAX_REVOKED_HISTORY_PER_ID:03}"))
        .expect("revoke");
    assert_eq!(
        registry.register(builtin("cap.g", "overflow")),
        Err(RegistryError::RevocationHistoryFull)
    );
    let view = registry.find("cap.g").expect("record intact");
    assert_eq!(
        view.descriptor.version,
        format!("v{MAX_REVOKED_HISTORY_PER_ID:03}")
    );
    assert_eq!(view.state, LifecycleState::Revoked);
}

/// Deterministic pseudo-random sequence (LCG, same technique as the
/// gateway tests): bounded size, precedence never decreases per id, and a
/// revoked pair can never resurrect.
#[test]
fn lcg_storm_invariants() {
    const IDS: [&str; 3] = ["storm.a", "storm.b", "storm.c"];
    const VERSIONS: [&str; 3] = ["1", "2", "3"];
    let sources = [
        (
            CapabilitySource::Partner,
            partner as fn(&str, &str) -> CapabilityDescriptor,
        ),
        (CapabilitySource::PersonalSkill, personal),
        (CapabilitySource::Builtin, builtin),
    ];
    let mut state: u64 = 0x243F_6A88_85A3_08D3;
    let mut next = || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };
    let mut registry = CapabilityRegistry::new();
    let mut revoked_pairs: Vec<(String, String)> = Vec::new();
    // Highest source precedence that ever registered successfully per id;
    // the live record must never drop below it.
    let mut max_precedence = [0_u8; 3];
    for _ in 0..3_000_u64 {
        let id_index = (next() % IDS.len() as u64) as usize;
        let id = IDS[id_index];
        let (source_index, make) = {
            let index = (next() % sources.len() as u64) as usize;
            (index, sources[index].1)
        };
        let version = VERSIONS[(next() % VERSIONS.len() as u64) as usize];
        match next() % 5 {
            0 | 1 => {
                if registry.register(make(id, version)).is_ok() {
                    max_precedence[id_index] =
                        max_precedence[id_index].max(sources[source_index].0.precedence());
                }
            }
            2 => {
                if registry.revoke(id, version).is_ok() {
                    let pair = (id.to_owned(), version.to_owned());
                    if !revoked_pairs.contains(&pair) {
                        revoked_pairs.push(pair);
                    }
                }
            }
            3 => {
                let _ = registry.set_enabled(id, version, next() % 2 == 0);
            }
            _ => {
                let _ = registry.find_version(id, version);
            }
        }
        // Invariants after every operation.
        assert!(registry.len() <= MAX_REGISTRATIONS);
        if let Some(view) = registry.find(id) {
            assert!(
                view.descriptor.source.precedence() >= max_precedence[id_index],
                "live precedence for {id} must never drop below the registered maximum"
            );
        }
        for (revoked_id, revoked_version) in &revoked_pairs {
            if let Some(view) = registry.find_version(revoked_id, revoked_version) {
                assert_eq!(
                    view.state,
                    LifecycleState::Revoked,
                    "revoked pair {revoked_id}@{revoked_version} must stay revoked"
                );
            }
        }
        let snapshot = registry.snapshot();
        let lines: Vec<&str> = snapshot.lines().collect();
        let mut sorted = lines.clone();
        sorted.sort_unstable();
        assert_eq!(lines, sorted, "snapshot stays id-sorted");
    }
    assert!(!revoked_pairs.is_empty(), "storm exercised revocations");
    assert!(revoked_pairs.iter().all(|(id, version)| {
        registry
            .find_version(id, version)
            .map(|view| view.state == LifecycleState::Revoked)
            .unwrap_or(false)
    }));
}
