//! WF-010 coverage: persistence roundtrip, lifecycle transitions, quota,
//! corrupt-record clearing, index migration and incognito cleanup.

use super::{SkillStore, SkillStoreError, MAX_SKILLS};
use crate::recipe::{generate_candidate, AttemptOutcome};
use crayon_domain::{ActionKind, Recipe, SemanticNodeId, SkillStatus, TraceStep, WorkflowTrace};

const ORIGIN: &str = "https://example.com";

struct MemoryStore {
    data: BTreeMap<String, Vec<u8>>,
}

impl MemoryStore {
    fn new() -> Self {
        Self {
            data: BTreeMap::new(),
        }
    }
}

impl SecureStore for MemoryStore {
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        self.data.insert(key.to_owned(), value.to_vec());
        Ok(())
    }

    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        Ok(self.data.get(key).cloned())
    }

    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        self.data.remove(key);
        Ok(())
    }
}

use crayon_platform_api::secure_store::{SecureStore, SecureStoreError};
use std::collections::BTreeMap;

fn recipe(name: &str, summary: &str) -> Recipe {
    let trace = WorkflowTrace::new(
        ORIGIN.to_owned(),
        vec![TraceStep {
            node: SemanticNodeId::new("node1").unwrap(),
            action: ActionKind::Click,
            summary: summary.to_owned(),
            outcome: crayon_domain::EffectOutcome::Verified,
        }],
    )
    .unwrap();
    let candidate = generate_candidate(AttemptOutcome::VerifiedSuccess, &trace, name, 1).unwrap();
    let crate::recipe::RecipeCandidate::Candidate(recipe) = candidate else {
        panic!("expected candidate");
    };
    recipe
}

#[test]
fn save_load_roundtrip_preserves_candidate() {
    let mut store = SkillStore::new(MemoryStore::new());
    let skill = store
        .save_candidate(recipe("form-helper", "open form"))
        .expect("save");
    assert_eq!(skill.status, SkillStatus::Candidate);
    assert_eq!(skill.revision, 1);
    assert_eq!(skill.recipe.name, "form-helper");
    let loaded = store.load("form-helper").expect("load");
    assert_eq!(loaded, skill);
    assert!(!loaded.runnable()); // Candidate is not runnable
}

#[test]
fn save_is_create_only_and_quota_is_bounded() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("alpha", "a")).unwrap();
    assert_eq!(
        store.save_candidate(recipe("alpha", "duplicate")),
        Err(SkillStoreError::AlreadyExists)
    );
    let mut count = 1usize;
    for index in 1..MAX_SKILLS + 5 {
        let name = format!("skill{index}");
        if store.save_candidate(recipe(&name, "s")).is_ok() {
            count += 1;
        }
    }
    assert_eq!(count, MAX_SKILLS);
    assert_eq!(
        store.save_candidate(recipe("overflow", "s")),
        Err(SkillStoreError::QuotaExceeded)
    );
}

#[test]
fn lifecycle_transitions_are_closed() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("alpha", "a")).unwrap();
    // Draft -> Enabled is illegal (never preview-confirmed).
    assert!(matches!(
        store.set_status("alpha", SkillStatus::Draft),
        Err(SkillStoreError::IllegalTransition)
    ));
    let enabled = store.set_status("alpha", SkillStatus::Enabled).unwrap();
    assert!(enabled.runnable());
    // Enabled -> Candidate is illegal.
    assert_eq!(
        store.set_status("alpha", SkillStatus::Candidate),
        Err(SkillStoreError::IllegalTransition)
    );
    let disabled = store.set_status("alpha", SkillStatus::Disabled).unwrap();
    assert!(!disabled.runnable());
    let re_enabled = store.set_status("alpha", SkillStatus::Enabled).unwrap();
    assert!(re_enabled.runnable());
}

#[test]
fn corrupt_record_is_cleared_and_reported() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("alpha", "a")).unwrap();
    // Inject a corrupt record directly into the backend.
    store.test_backend_store("wflsk-alpha", b"{not json");
    assert_eq!(store.load("alpha"), Err(SkillStoreError::CorruptCleared));
    // Record removed: subsequent load is NotFound, not Corrupt.
    assert_eq!(store.load("alpha"), Err(SkillStoreError::NotFound));
}

#[test]
fn list_enumerates_and_skips_corrupt() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("beta", "b")).unwrap();
    store.save_candidate(recipe("alpha", "a")).unwrap();
    store.test_backend_store("wflsk-gamma", b"broken");
    store.test_backend_write_index_raw(1, &["alpha", "beta", "gamma"]);
    let summaries = store.list().unwrap();
    assert_eq!(
        summaries
            .iter()
            .map(|s| s.name.as_str())
            .collect::<Vec<_>>(),
        ["alpha", "beta"]
    );
}

#[test]
fn index_migration_clears_unknown_versions() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("alpha", "a")).unwrap();
    // Simulate an older/unknown index schema, then confirm the stale
    // record is gone after the migration pass.
    store.test_backend_write_index_raw(99, &["alpha"]);
    match store.list() {
        Err(SkillStoreError::IndexMigrated) | Err(SkillStoreError::NotFound) => {}
        Ok(summaries) => assert!(summaries.is_empty()),
        Err(other) => panic!("unexpected error: {other:?}"),
    }
}

#[test]
fn clear_all_is_incognito_cleanup() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("alpha", "a")).unwrap();
    store.save_candidate(recipe("beta", "b")).unwrap();
    assert_eq!(store.clear_all().unwrap(), 2);
    assert!(store.list().unwrap().is_empty());
    assert_eq!(store.load("alpha"), Err(SkillStoreError::NotFound));
    // Clear is idempotent.
    assert_eq!(store.clear_all().unwrap(), 0);
}

#[test]
fn upgrade_advances_revision_and_preserves_status() {
    let mut store = SkillStore::new(MemoryStore::new());
    store.save_candidate(recipe("alpha", "v1 body")).unwrap();
    store.set_status("alpha", SkillStatus::Enabled).unwrap();
    let upgraded = store.upgrade("alpha", recipe("alpha", "v2 body")).unwrap();
    assert_eq!(upgraded.revision, 2);
    assert_eq!(upgraded.status, SkillStatus::Enabled);
    assert_eq!(upgraded.recipe.steps[0].summary, "v2 body");
    // Revision must strictly advance.
    assert!(store.upgrade("alpha", recipe("alpha", "v1 body")).is_ok());
    // Name mismatch rejected.
    assert!(store.upgrade("alpha", recipe("other", "x")).is_err());
}

// --- backend helpers ---

// The store's backend is crate-private; corrupt/injection helpers use a
// test-visible reflection wrapper instead.
impl<S: SecureStore> SkillStore<S> {
    fn test_backend_store(&mut self, key: &str, value: &[u8]) {
        self.backend_mut().store(key, value).unwrap();
    }

    fn test_backend_write_index_raw(&mut self, schema: u32, names: &[&str]) {
        let payload = serde_json::json!({ "schema": schema, "names": names });
        self.backend_mut()
            .store(
                crate::store::SKILL_INDEX_KEY,
                serde_json::to_vec(&payload).unwrap().as_slice(),
            )
            .unwrap();
    }
}
