//! Behaviour tests for persistent profile storage transactions.  Each test
//! uses a unique real temporary root; no shared fixture state exists.

use crayon_profile::{
    DestroyOutcome, DirectoryId, PersistentStore, PersistentStoreError, ProfileId, ProfileRegistry,
    ProfileType,
};
use std::fs;
use std::path::PathBuf;

struct TestRoot {
    path: PathBuf,
}

impl TestRoot {
    fn new() -> Self {
        let unique = DirectoryId::generate().expect("entropy").to_hex();
        let path = std::env::temp_dir().join(format!("crayon-profile-test-{unique}"));
        fs::create_dir_all(&path).expect("temp root");
        Self { path }
    }
}

impl Drop for TestRoot {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}

fn registry_with_profile(
    root: &TestRoot,
    name: &str,
    directory_byte: u8,
) -> (ProfileRegistry, ProfileId) {
    let mut registry = ProfileRegistry::new(root.path.clone()).expect("absolute root");
    registry
        .create_profile_with_directory(
            name,
            ProfileType::Regular,
            DirectoryId::from_bytes([directory_byte; 16]),
        )
        .expect("create profile");
    let id = ProfileId::new(name).expect("id");
    (registry, id)
}

fn close_profile(registry: &mut ProfileRegistry, id: &ProfileId) {
    registry.begin_close(id).expect("begin close");
    registry.finish_close(id).expect("finish close");
}

// ---------- Creation ----------

#[test]
fn create_space_writes_marker() {
    let root = TestRoot::new();
    let (registry, id) = registry_with_profile(&root, "work", 1);
    let store = PersistentStore::new(&registry);
    let path = store.create_space(&id).expect("create");
    assert!(path.is_dir());
    let marker = fs::read_to_string(path.join(".crayon-profile")).expect("marker");
    assert!(marker.contains("schema=1"));
    assert!(marker.contains("directory=01010101010101010101010101010101"));
}

#[test]
fn create_space_is_idempotent() {
    let root = TestRoot::new();
    let (registry, id) = registry_with_profile(&root, "work", 2);
    let store = PersistentStore::new(&registry);
    store.create_space(&id).expect("first create");
    let again = store.create_space(&id).expect("retry is success");
    assert!(again.is_dir());
}

#[test]
fn create_space_rejects_foreign_directory() {
    let root = TestRoot::new();
    let (registry, id) = registry_with_profile(&root, "work", 3);
    let store = PersistentStore::new(&registry);
    let path = registry.profile_path(&id).expect("path");
    fs::create_dir(&path).expect("precreate");
    fs::write(path.join(".crayon-profile"), "schema=1\ndirectory=ff\n").expect("marker");
    assert_eq!(
        store.create_space(&id),
        Err(PersistentStoreError::OwnershipMismatch)
    );
    // The foreign directory must remain untouched.
    assert!(path.is_dir());
}

#[test]
fn create_space_rejects_incognito_and_unknown() {
    let root = TestRoot::new();
    let (mut registry, _) = registry_with_profile(&root, "work", 4);
    registry
        .create_profile_with_directory(
            "private",
            ProfileType::Incognito,
            DirectoryId::from_bytes([9; 16]),
        )
        .expect("create incognito");
    let store = PersistentStore::new(&registry);
    let incognito = ProfileId::new("private").expect("id");
    assert_eq!(
        store.create_space(&incognito),
        Err(PersistentStoreError::EphemeralProfile)
    );
    let missing = ProfileId::new("missing").expect("id");
    assert_eq!(
        store.create_space(&missing),
        Err(PersistentStoreError::UnknownProfile)
    );
}

// ---------- Destruction ----------

#[test]
fn destroy_requires_closed_profile() {
    let root = TestRoot::new();
    let (mut registry, id) = registry_with_profile(&root, "work", 5);
    {
        let store = PersistentStore::new(&registry);
        store.create_space(&id).expect("create");
        assert_eq!(
            store.destroy_space(&id),
            Err(PersistentStoreError::IllegalState)
        );
    }
    registry.begin_close(&id).expect("begin close");
    {
        let store = PersistentStore::new(&registry);
        assert_eq!(
            store.destroy_space(&id),
            Err(PersistentStoreError::IllegalState) // closing, not closed
        );
    }
    registry.finish_close(&id).expect("finish close");
    let store = PersistentStore::new(&registry);
    assert_eq!(store.destroy_space(&id), Ok(DestroyOutcome::Removed));
    assert!(!registry.profile_path(&id).expect("path").exists());
    // Repeated destroy after removal is a stable no-op success.
    assert_eq!(store.destroy_space(&id), Ok(DestroyOutcome::Removed));
}

#[test]
fn destroy_rejects_tampered_marker() {
    let root = TestRoot::new();
    let (mut registry, id) = registry_with_profile(&root, "work", 6);
    {
        let store = PersistentStore::new(&registry);
        store.create_space(&id).expect("create");
        let path = registry.profile_path(&id).expect("path");
        fs::write(path.join(".crayon-profile"), "tampered").expect("tamper");
    }
    close_profile(&mut registry, &id);
    let store = PersistentStore::new(&registry);
    assert_eq!(
        store.destroy_space(&id),
        Err(PersistentStoreError::OwnershipMismatch)
    );
    // Fail closed: the directory with the tampered marker still exists.
    assert!(registry.profile_path(&id).expect("path").exists());
}

#[test]
fn destroy_rejects_incognito() {
    let root = TestRoot::new();
    let mut registry = ProfileRegistry::new(root.path.clone()).expect("root");
    registry
        .create_profile_with_directory(
            "private",
            ProfileType::Incognito,
            DirectoryId::from_bytes([8; 16]),
        )
        .expect("create");
    let id = ProfileId::new("private").expect("id");
    close_profile(&mut registry, &id);
    let store = PersistentStore::new(&registry);
    assert_eq!(
        store.destroy_space(&id),
        Err(PersistentStoreError::EphemeralProfile)
    );
}

// ---------- Resume after partial failure ----------

#[test]
fn retry_pending_destroys_resumes_staging_dirs() {
    let root = TestRoot::new();
    let registry = ProfileRegistry::new(root.path.clone()).expect("root");
    let store = PersistentStore::new(&registry);
    // Simulate a partially-failed destroy: a leftover staging directory.
    let staging = root
        .path
        .join("07070707070707070707070707070707.deleting-0");
    fs::create_dir(&staging).expect("staging");
    fs::write(staging.join("leftover.bin"), b"partial").expect("file");
    assert_eq!(store.retry_pending_destroys().expect("resume"), 0);
    assert!(!staging.exists());
}

#[test]
fn retry_pending_destroys_is_bounded() {
    let root = TestRoot::new();
    let registry = ProfileRegistry::new(root.path.clone()).expect("root");
    let store = PersistentStore::new(&registry);
    for index in 0..20 {
        let dir = DirectoryId::from_bytes([index as u8; 16]).to_hex();
        fs::create_dir(root.path.join(format!("{dir}.deleting-0"))).expect("staging");
    }
    let remaining = store.retry_pending_destroys().expect("resume");
    assert!(remaining <= 4, "at most 4 of 20 remain after resuming 16");
    // Repeat calls keep making progress.
    assert_eq!(store.retry_pending_destroys().expect("resume"), 0);
}
