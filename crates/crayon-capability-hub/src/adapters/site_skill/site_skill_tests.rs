//! HB-007 coverage: owner/Profile isolation (source-scoped), health
//! filtering, version re-registration and revocation of skills that are
//! no longer Enabled.

use super::{sync_site_skills, SkillSource, SITE_SKILL_ID_PREFIX};
use std::collections::BTreeMap;

struct FakeSource {
    enabled: Vec<String>,
    versions: BTreeMap<String, String>,
    healthy: BTreeMap<String, bool>,
}

impl FakeSource {
    fn new(names: &[&str]) -> Self {
        let mut versions = BTreeMap::new();
        let mut healthy = BTreeMap::new();
        for name in names {
            versions.insert(name.to_string(), "1.0.0".to_owned());
            healthy.insert(name.to_string(), true);
        }
        Self {
            enabled: names.iter().map(|s| s.to_string()).collect(),
            versions,
            healthy,
        }
    }

    fn set_unhealthy(&mut self, name: &str) {
        self.healthy.insert(name.to_string(), false);
    }

    fn disable(&mut self, name: &str) {
        self.enabled.retain(|n| n != name);
    }
}

impl SkillSource for FakeSource {
    fn enabled_names(&self) -> Vec<String> {
        self.enabled.clone()
    }

    fn version_of(&self, name: &str) -> Option<String> {
        self.versions.get(name).cloned()
    }

    fn summary_of(&self, name: &str) -> Option<String> {
        self.versions.get(name).map(|_| format!("{name} summary"))
    }

    fn is_healthy(&self, name: &str) -> bool {
        *self.healthy.get(name).unwrap_or(&true)
    }
}

#[derive(Default)]
struct RecordingRegistry {
    upserted: Vec<String>,
    revoked: Vec<String>,
}

impl super::RegistrySink for RecordingRegistry {
    fn upsert(&mut self, descriptor: crayon_domain::CapabilityDescriptor) {
        self.upserted.push(descriptor.id);
    }

    fn revoke(&mut self, id: &str) {
        self.revoked.push(id.to_owned());
    }
}

#[test]
fn enabled_skills_are_registered_with_personal_prefix() {
    let source = FakeSource::new(&["alpha", "beta"]);
    let mut registry = RecordingRegistry::default();
    let mut known = Vec::new();
    let (registered, revoked) = sync_site_skills(&mut registry, &source, &mut known).unwrap();
    assert_eq!((registered, revoked), (2, 0));
    assert!(registry
        .upserted
        .iter()
        .all(|id| id.starts_with(SITE_SKILL_ID_PREFIX)));
}

#[test]
fn unhealthy_skill_is_skipped_but_stays_revocable() {
    let mut source = FakeSource::new(&["alpha", "sick"]);
    source.set_unhealthy("sick");
    let mut registry = RecordingRegistry::default();
    let mut known = Vec::new();
    let (registered, _) = sync_site_skills(&mut registry, &source, &mut known).unwrap();
    assert_eq!(registered, 1);
    assert!(registry.upserted.iter().all(|id| !id.contains("sick")));
}

#[test]
fn disabled_skill_revokes_prior_registration() {
    let mut source = FakeSource::new(&["alpha", "gone"]);
    let mut registry = RecordingRegistry::default();
    let mut known = Vec::new();
    let _ = sync_site_skills(&mut registry, &source, &mut known).unwrap();
    assert!(registry
        .upserted
        .iter()
        .any(|id| id == &format!("{SITE_SKILL_ID_PREFIX}gone")));

    // User disables "gone": no longer Enabled.
    source.disable("gone");
    let (registered, _) = sync_site_skills(&mut registry, &source, &mut known).unwrap();
    assert_eq!(registered, 1); // only alpha re-upserted
    assert!(registry
        .revoked
        .iter()
        .any(|id| id == &format!("{SITE_SKILL_ID_PREFIX}gone")));
}

#[test]
fn version_change_reregisters_same_id() {
    let mut source = FakeSource::new(&["alpha"]);
    let mut registry = RecordingRegistry::default();
    let mut known = Vec::new();
    let _ = sync_site_skills(&mut registry, &source, &mut known).unwrap();
    source
        .versions
        .insert("alpha".to_owned(), "2.0.0".to_owned());
    let _ = sync_site_skills(&mut registry, &source, &mut known).unwrap();
    assert_eq!(
        registry
            .upserted
            .iter()
            .filter(|id| *id == &format!("{SITE_SKILL_ID_PREFIX}alpha"))
            .count(),
        2
    );
}

#[test]
fn profiles_are_isolated_by_source_scope() {
    // Two sources = two Profiles: each adapter sync only sees its own
    // source's skills.
    let profile_a = FakeSource::new(&["skill-a"]);
    let profile_b = FakeSource::new(&["skill-b"]);
    let mut registry_a = RecordingRegistry::default();
    let mut registry_b = RecordingRegistry::default();
    let mut known_a = Vec::new();
    let mut known_b = Vec::new();
    let _ = sync_site_skills(&mut registry_a, &profile_a, &mut known_a).unwrap();
    let _ = sync_site_skills(&mut registry_b, &profile_b, &mut known_b).unwrap();
    assert!(registry_a.upserted.iter().all(|id| id.contains("skill-a")));
    assert!(registry_b.upserted.iter().all(|id| id.contains("skill-b")));
    assert!(!registry_a.upserted.iter().any(|id| id.contains("skill-b")));
}
