//! Encrypted personal skill store (WFL-10).
//!
//! Encryption and OS-user/Profile isolation are owned by the injected
//! platform `SecureStore` (WFL-04 pattern). Skills are persisted as
//! validated `SiteSkill` records under the `wflsk-` key prefix with a
//! separate schema-versioned index record for enumeration. Corrupt or
//! unknown-version records are cleared and counted — fail closed.

use std::collections::BTreeMap;

use crayon_domain::{Recipe, SiteSkill, SkillError, SkillStatus, MAX_SKILL_REVISION};
use crayon_platform_api::secure_store::{validate_key, SecureStore, SecureStoreError};

/// Key prefix for every persisted skill record.
pub const SKILL_KEY_PREFIX: &str = "wflsk-";
/// Index record key holding the persisted skill names.
pub const SKILL_INDEX_KEY: &str = "wflsk-index";
/// Index record schema version. A mismatched index is migrated: unknown
/// records are cleared and the migration counter increments.
pub const SKILL_INDEX_SCHEMA_VERSION: u32 = 1;
/// Maximum persisted skills; a full store refuses new skills (fail closed).
pub const MAX_SKILLS: usize = 64;

/// Store failure; variants carry no user content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SkillStoreError {
    /// Skill name failed the platform key validation.
    InvalidName,
    /// The store is at quota; `save` was refused (fail closed).
    QuotaExceeded,
    /// The skill record existed but could not be parsed; it was cleared.
    CorruptCleared,
    /// The index record version is unknown; stale records were cleared.
    IndexMigrated,
    /// The skill is not in the store.
    NotFound,
    /// A skill with this name already exists (save is create-only).
    AlreadyExists,
    /// Closed lifecycle transition rejected.
    IllegalTransition,
    /// Recipe or skill failed domain validation.
    DomainRejected,
    /// The injected store backend failed.
    Backend(SecureStoreError),
}

impl std::fmt::Display for SkillStoreError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidName => formatter.write_str("skill key rejected"),
            Self::QuotaExceeded => formatter.write_str("skill quota exceeded"),
            Self::CorruptCleared => formatter.write_str("corrupt skill record cleared"),
            Self::IndexMigrated => formatter.write_str("index migrated; stale records cleared"),
            Self::NotFound => formatter.write_str("skill not found"),
            Self::AlreadyExists => formatter.write_str("skill name already exists"),
            Self::IllegalTransition => formatter.write_str("skill status transition not allowed"),
            Self::DomainRejected => formatter.write_str("domain validation rejected the skill"),
            Self::Backend(error) => error.fmt(formatter),
        }
    }
}

impl std::error::Error for SkillStoreError {}

impl From<SkillError> for SkillStoreError {
    fn from(_: SkillError) -> Self {
        Self::DomainRejected
    }
}

/// Summary row for `list`: identity plus lifecycle state.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SkillSummary {
    pub name: String,
    pub version: u32,
    pub status: SkillStatus,
    pub revision: u64,
}

/// Profile-scoped skill store.
pub struct SkillStore<S: SecureStore> {
    store: S,
}

fn skill_key(name: &str) -> Result<String, SkillStoreError> {
    let key = format!("{SKILL_KEY_PREFIX}{name}");
    validate_key(&key).map_err(|_| SkillStoreError::InvalidName)?;
    Ok(key)
}

/// The persisted index: names + index schema version.
#[derive(serde::Serialize, serde::Deserialize)]
struct StoreIndex {
    schema: u32,
    names: Vec<String>,
}

impl<S: SecureStore> SkillStore<S> {
    #[must_use]
    pub fn new(store: S) -> Self {
        Self { store }
    }

    fn load_index(&mut self) -> Result<BTreeMap<String, ()>, SkillStoreError> {
        let Some(bytes) = self
            .store
            .load(SKILL_INDEX_KEY)
            .map_err(SkillStoreError::Backend)?
        else {
            return Ok(BTreeMap::new());
        };
        match serde_json::from_slice::<StoreIndex>(&bytes) {
            Ok(index) if index.schema == SKILL_INDEX_SCHEMA_VERSION => {
                Ok(index.names.into_iter().map(|name| (name, ())).collect())
            }
            _ => {
                // Unknown index version: migrate by clearing the records it
                // names (their schema is unverifiable) and dropping the
                // stale index. Callers retry with a fresh store.
                if let Ok(old) = serde_json::from_slice::<StoreIndex>(&bytes) {
                    for name in old.names {
                        let key = format!("{SKILL_KEY_PREFIX}{name}");
                        self.store.delete(&key).map_err(SkillStoreError::Backend)?;
                    }
                }
                self.store
                    .delete(SKILL_INDEX_KEY)
                    .map_err(SkillStoreError::Backend)?;
                Err(SkillStoreError::IndexMigrated)
            }
        }
    }

    fn write_index(&mut self, names: &[String]) -> Result<(), SkillStoreError> {
        let index = StoreIndex {
            schema: SKILL_INDEX_SCHEMA_VERSION,
            names: names.to_vec(),
        };
        let bytes = serde_json::to_vec(&index).map_err(|_| SkillStoreError::InvalidName)?;
        self.store
            .store(SKILL_INDEX_KEY, &bytes)
            .map_err(SkillStoreError::Backend)?;
        Ok(())
    }

    /// Persists a user-confirmed candidate as a new `Candidate` skill
    /// (revision 1). Create-only: an existing name is rejected.
    pub fn save_candidate(&mut self, recipe: Recipe) -> Result<SiteSkill, SkillStoreError> {
        let mut index = self.load_index()?;
        if index.contains_key(&recipe.name) {
            return Err(SkillStoreError::AlreadyExists);
        }
        if index.len() >= MAX_SKILLS {
            return Err(SkillStoreError::QuotaExceeded);
        }
        let skill = SiteSkill::new(recipe, SkillStatus::Candidate, 1)?;
        let key = skill_key(&skill.recipe.name)?;
        let bytes = serde_json::to_vec(&skill).map_err(|_| SkillStoreError::DomainRejected)?;
        self.store
            .store(&key, &bytes)
            .map_err(SkillStoreError::Backend)?;
        index.insert(skill.recipe.name.clone(), ());
        let names: Vec<String> = index.keys().cloned().collect();
        self.write_index(&names)?;
        Ok(skill)
    }

    /// Loads one skill by name. A corrupt record is deleted and reported
    /// as `CorruptCleared` (fail closed, no partial data).
    pub fn load(&mut self, name: &str) -> Result<SiteSkill, SkillStoreError> {
        let key = skill_key(name)?;
        let Some(bytes) = self.store.load(&key).map_err(SkillStoreError::Backend)? else {
            return Err(SkillStoreError::NotFound);
        };
        match serde_json::from_slice::<SiteSkill>(&bytes) {
            Ok(skill) => Ok(skill),
            Err(_) => {
                self.store.delete(&key).map_err(SkillStoreError::Backend)?;
                Err(SkillStoreError::CorruptCleared)
            }
        }
    }

    /// Enumerates persisted skills. Corrupt records are cleared and
    /// skipped.
    pub fn list(&mut self) -> Result<Vec<SkillSummary>, SkillStoreError> {
        let index = self.load_index()?;
        let mut summaries = Vec::new();
        let mut corrupted = Vec::new();
        let names: Vec<String> = index.keys().cloned().collect();
        for name in &names {
            match self.load(name) {
                Ok(skill) => summaries.push(SkillSummary {
                    name: skill.recipe.name.clone(),
                    version: skill.recipe.version,
                    status: skill.status,
                    revision: skill.revision,
                }),
                Err(SkillStoreError::CorruptCleared) => corrupted.push(name.clone()),
                Err(error) => return Err(error),
            }
        }
        for name in corrupted {
            let key = skill_key(&name)?;
            self.store.delete(&key).map_err(SkillStoreError::Backend)?;
        }
        summaries.sort_by(|left, right| left.name.cmp(&right.name));
        Ok(summaries)
    }

    /// Closed lifecycle transitions. Only `Candidate -> Enabled`,
    /// `Candidate -> Disabled` and `Enabled <-> Disabled` are allowed.
    pub fn set_status(
        &mut self,
        name: &str,
        status: SkillStatus,
    ) -> Result<SiteSkill, SkillStoreError> {
        let skill = self.load(name)?;
        let allowed = matches!(
            (skill.status, status),
            (SkillStatus::Candidate, SkillStatus::Enabled)
                | (SkillStatus::Candidate, SkillStatus::Disabled)
                | (SkillStatus::Enabled, SkillStatus::Disabled)
                | (SkillStatus::Disabled, SkillStatus::Enabled)
        );
        if !allowed {
            return Err(SkillStoreError::IllegalTransition);
        }
        let updated = SiteSkill::new(skill.recipe.clone(), status, skill.revision)?;
        let key = skill_key(name)?;
        let bytes = serde_json::to_vec(&updated).map_err(|_| SkillStoreError::DomainRejected)?;
        self.store
            .store(&key, &bytes)
            .map_err(SkillStoreError::Backend)?;
        Ok(updated)
    }

    /// Bumps the revision and replaces the recipe for an existing skill
    /// (WFL-08 regenerated candidate over an existing name via explicit
    /// upgrade). Status is preserved; revision must advance.
    pub fn upgrade(&mut self, name: &str, recipe: Recipe) -> Result<SiteSkill, SkillStoreError> {
        let existing = self.load(name)?;
        if recipe.name != name {
            return Err(SkillStoreError::InvalidName);
        }
        let revision = existing
            .revision
            .checked_add(1)
            .filter(|revision| *revision <= MAX_SKILL_REVISION)
            .ok_or(SkillStoreError::DomainRejected)?;
        let updated = SiteSkill::new(recipe, existing.status, revision)?;
        let key = skill_key(name)?;
        let bytes = serde_json::to_vec(&updated).map_err(|_| SkillStoreError::DomainRejected)?;
        self.store
            .store(&key, &bytes)
            .map_err(SkillStoreError::Backend)?;
        Ok(updated)
    }

    /// Deletes one skill. Idempotent; keeps the index in sync.
    pub fn delete(&mut self, name: &str) -> Result<(), SkillStoreError> {
        let key = skill_key(name)?;
        self.store.delete(&key).map_err(SkillStoreError::Backend)?;
        let mut index = self.load_index()?;
        if index.remove(name).is_some() {
            let names: Vec<String> = index.keys().cloned().collect();
            self.write_index(&names)?;
        }
        Ok(())
    }

    /// Incognito cleanup: deletes every skill and the index. Returns the
    /// number of skills removed.
    pub fn clear_all(&mut self) -> Result<usize, SkillStoreError> {
        let index = self.load_index()?;
        let mut cleared = 0usize;
        let names: Vec<String> = index.keys().cloned().collect();
        for name in &names {
            let key = skill_key(name)?;
            self.store.delete(&key).map_err(SkillStoreError::Backend)?;
            cleared += 1;
        }
        self.store
            .delete(SKILL_INDEX_KEY)
            .map_err(SkillStoreError::Backend)?;
        Ok(cleared)
    }
}

#[cfg(test)]
mod store_tests;

#[cfg(test)]
impl<S: SecureStore> SkillStore<S> {
    pub(crate) fn backend_mut(&mut self) -> &mut S {
        &mut self.store
    }
}
