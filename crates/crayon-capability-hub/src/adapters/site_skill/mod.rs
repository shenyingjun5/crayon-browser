//! Site Skill registry adapter (HUB-07).
//!
//! Bridges the user's personal Site Skills (WFL-10 store) into the
//! capability registry: every `Enabled` skill of the current Profile
//! becomes a `PersonalSkill` capability descriptor. Isolation rules:
//! - only the injected source's Profile is visible (the source is
//!   Profile-scoped by construction);
//! - only `Enabled` skills are exposed — Draft/Candidate/Disabled are
//!   never registered;
//! - health: a skill the health layer disabled is skipped (HUB-13 owns
//!   the disable decision; the adapter consumes the verdict);
//! - versions: the skill recipe version is the descriptor version, so a
//!   version change re-registers under the same id.

use crayon_domain::{CapabilityDescriptor, CapabilitySource, DataScope, TrustLevel};

/// Registry key prefix for personal skills (no overlap with builtin or
/// partner namespaces).
pub const SITE_SKILL_ID_PREFIX: &str = "personal-skill.";

/// Read-only view of the WFL-10 store for the current Profile. Only
/// `Enabled` skills are surfaced; health-disabled skills are filtered by
/// the injected health predicate.
pub trait SkillSource {
    /// Names of the caller's currently Enabled skills.
    fn enabled_names(&self) -> Vec<String>;
    /// Loads one skill's recipe version.
    fn version_of(&self, name: &str) -> Option<String>;
    /// Loads one skill's summary (first step summary or name).
    fn summary_of(&self, name: &str) -> Option<String>;
    /// Health verdict: false when the health layer disabled the skill.
    fn is_healthy(&self, name: &str) -> bool;
}

/// Adapter error; content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AdapterError {
    /// The skill failed domain validation during registration.
    SkillRejected,
    /// The underlying registry refused the descriptor.
    RegistryRejected,
}

/// Registry sink so the adapter stays decoupled from the concrete
/// registry type (the real registry lives in the same crate; tests inject
/// a recording sink).
pub trait RegistrySink {
    /// Registers or replaces the descriptor for its id.
    fn upsert(&mut self, descriptor: CapabilityDescriptor);
    /// Revokes the descriptor for `id` (any version).
    fn revoke(&mut self, id: &str);
}

/// Syncs the registry with the current Profile's Enabled skills: every
/// healthy Enabled skill is upserted; a previously-registered skill that
/// is no longer Enabled (or is health-disabled) is revoked.
///
/// Returns (registered, revoked) counts.
pub fn sync_site_skills<S: SkillSource>(
    registry: &mut dyn RegistrySink,
    source: &S,
    previously_registered: &mut Vec<String>,
) -> Result<(usize, usize), AdapterError> {
    let enabled = source.enabled_names();
    let mut registered = 0usize;
    let mut revoked = 0usize;
    let mut live: Vec<String> = Vec::new();
    for name in &enabled {
        if !source.is_healthy(name) {
            continue;
        }
        let Some(version) = source.version_of(name) else {
            continue;
        };
        let summary = source.summary_of(name).unwrap_or_else(|| name.clone());
        let descriptor = CapabilityDescriptor {
            id: format!("{SITE_SKILL_ID_PREFIX}{name}"),
            version,
            source: CapabilitySource::PersonalSkill,
            trust: TrustLevel::UserApproved,
            data_scope: DataScope::PageContent,
            summary,
        };
        descriptor
            .validate()
            .map_err(|_| AdapterError::SkillRejected)?;
        registry.upsert(descriptor);
        registered += 1;
        let qualified = format!("{SITE_SKILL_ID_PREFIX}{name}");
        live.push(qualified.clone());
        previously_registered.push(qualified);
    }
    // Revoke previously-registered skills that are no longer live.
    for id in previously_registered.iter() {
        if !live.iter().any(|name| name == id) {
            registry.revoke(id);
            revoked += 1;
        }
    }
    *previously_registered = live;
    Ok((registered, revoked))
}

#[cfg(test)]
mod site_skill_tests;
