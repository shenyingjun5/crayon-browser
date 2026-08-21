//! Behaviour tests for the path guard and its integration with persistent
//! storage transactions (PV-006: symlink/junction escape attempts are
//! refused and external files stay untouched).

use crayon_profile::{
    DirectoryId, PathGuard, PathGuardError, PersistentStore, PersistentStoreError, ProfileId,
    ProfileRegistry, ProfileType, MAX_CLEANUP_PER_CALL,
};
use std::fs;
use std::path::{Path, PathBuf};

struct TestRoot {
    path: PathBuf,
}

impl TestRoot {
    fn new() -> Self {
        let unique = DirectoryId::generate().expect("entropy").to_hex();
        let path = std::env::temp_dir().join(format!("crayon-pathguard-test-{unique}"));
        fs::create_dir_all(&path).expect("temp root");
        Self { path }
    }
}

impl Drop for TestRoot {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}

/// Creates a symlink; only Unix tests exercise escape construction because
/// Windows symlink creation requires privileges the CI sandbox may lack.
#[cfg(unix)]
fn symlink(target: &Path, link: &Path) {
    std::os::unix::fs::symlink(target, link).expect("symlink");
}

// ---------- Root verification ----------

#[test]
fn root_must_be_absolute_existing_directory() {
    assert_eq!(
        PathGuard::new(Path::new("relative/root")).map(|_| ()),
        Err(PathGuardError::RootInvalid)
    );
    let root = TestRoot::new();
    assert_eq!(
        PathGuard::new(&root.path.join("missing")).map(|_| ()),
        Err(PathGuardError::RootInvalid)
    );
    let file = root.path.join("a-file");
    fs::write(&file, b"x").expect("file");
    assert_eq!(
        PathGuard::new(&file).map(|_| ()),
        Err(PathGuardError::RootInvalid)
    );
    let guard = PathGuard::new(&root.path).expect("valid root");
    assert!(guard.root().is_absolute());
    assert!(guard.root().is_dir());
}

// ---------- Relative-path shape ----------

#[test]
fn relative_path_shape_is_enforced() {
    let root = TestRoot::new();
    let guard = PathGuard::new(&root.path).expect("guard");
    fs::create_dir(root.path.join("a")).expect("dir");

    // Empty resolves to the root itself and must be rejected.
    assert_eq!(
        guard.verify_inside(Path::new("")).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    // Absolute paths are rejected even when they point inside the root.
    let absolute = root.path.join("a");
    assert_eq!(
        guard.verify_inside(&absolute).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    assert_eq!(
        guard.verify_inside(Path::new("..")).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    assert_eq!(
        guard.verify_inside(Path::new("a/..")).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    assert_eq!(
        guard.verify_inside(Path::new("./a")).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    // Depth bound: five nested components exceed the limit of four.
    assert_eq!(
        guard.verify_inside(Path::new("a/b/c/d/e")).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    // Length bound.
    let long = format!("a/{}", "x".repeat(300));
    assert_eq!(
        guard.verify_inside(Path::new(&long)).map(|_| ()),
        Err(PathGuardError::InvalidRelative)
    );
    // Missing components fail as I/O, never as success.
    assert_eq!(
        guard.verify_inside(Path::new("missing")).map(|_| ()),
        Err(PathGuardError::Io)
    );
    // A real directory verifies and anchors under the canonical root.
    let verified = guard.verify_inside(Path::new("a")).expect("verify");
    assert!(verified.starts_with(guard.root()));
}

// ---------- Escape protection (PV-006) ----------

#[cfg(unix)]
#[test]
fn symlink_target_escape_is_refused_and_untouched() {
    let root = TestRoot::new();
    let outside = TestRoot::new();
    let sentinel = outside.path.join("sentinel.txt");
    fs::write(&sentinel, b"do-not-touch").expect("sentinel");

    // The profile directory is replaced by a symlink to the outside.
    symlink(&outside.path, &root.path.join("escaped"));

    let guard = PathGuard::new(&root.path).expect("guard");
    assert_eq!(
        guard.verify_inside(Path::new("escaped")).map(|_| ()),
        Err(PathGuardError::EscapeDetected)
    );
    assert_eq!(
        guard.remove_tree(Path::new("escaped")),
        Err(PathGuardError::EscapeDetected)
    );
    // Zero modification: sentinel and the symlink itself both survive.
    assert_eq!(
        fs::read_to_string(&sentinel).expect("sentinel"),
        "do-not-touch"
    );
    assert!(root.path.join("escaped").symlink_metadata().is_ok());
}

#[cfg(unix)]
#[test]
fn intermediate_symlink_component_is_refused() {
    let root = TestRoot::new();
    let outside = TestRoot::new();
    fs::create_dir(outside.path.join("inner")).expect("inner");
    symlink(&outside.path, &root.path.join("link"));

    let guard = PathGuard::new(&root.path).expect("guard");
    assert_eq!(
        guard.verify_inside(Path::new("link/inner")).map(|_| ()),
        Err(PathGuardError::EscapeDetected)
    );
}

#[test]
fn remove_tree_removes_real_directory() {
    let root = TestRoot::new();
    let guard = PathGuard::new(&root.path).expect("guard");
    let nested = root.path.join("d1/d2");
    fs::create_dir_all(&nested).expect("nested");
    fs::write(nested.join("f.bin"), b"data").expect("file");
    guard.remove_tree(Path::new("d1")).expect("remove");
    assert!(!root.path.join("d1").exists());
}

// ---------- Compensating cleanup ----------

#[test]
fn cleanup_staging_is_bounded_and_ignores_other_entries() {
    let root = TestRoot::new();
    let guard = PathGuard::new(&root.path).expect("guard");
    fs::create_dir(root.path.join("keep-me")).expect("regular dir");
    for index in 0..20_u8 {
        let dir = DirectoryId::from_bytes([index; 16]).to_hex();
        fs::create_dir(root.path.join(format!("{dir}.deleting-0"))).expect("staging");
    }
    let remaining = guard
        .cleanup_staging(MAX_CLEANUP_PER_CALL)
        .expect("cleanup");
    assert_eq!(remaining, 4);
    assert!(root.path.join("keep-me").is_dir());
    assert_eq!(
        guard.cleanup_staging(MAX_CLEANUP_PER_CALL).expect("again"),
        0
    );
}

#[cfg(unix)]
#[test]
fn cleanup_staging_never_follows_symlink_entries() {
    let root = TestRoot::new();
    let outside = TestRoot::new();
    let sentinel = outside.path.join("sentinel.txt");
    fs::write(&sentinel, b"do-not-touch").expect("sentinel");

    let link = root
        .path
        .join("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.deleting-0");
    symlink(&outside.path, &link);

    let guard = PathGuard::new(&root.path).expect("guard");
    // The escape entry is skipped and reported as remaining.
    assert_eq!(
        guard
            .cleanup_staging(MAX_CLEANUP_PER_CALL)
            .expect("cleanup"),
        1
    );
    assert_eq!(
        fs::read_to_string(&sentinel).expect("sentinel"),
        "do-not-touch"
    );
    assert!(link.symlink_metadata().is_ok());
}

// ---------- PersistentStore integration ----------

fn closed_registry_with_space(root: &TestRoot, directory_byte: u8) -> (ProfileRegistry, ProfileId) {
    let mut registry = ProfileRegistry::new(root.path.clone()).expect("root");
    registry
        .create_profile_with_directory(
            "work",
            ProfileType::Regular,
            DirectoryId::from_bytes([directory_byte; 16]),
        )
        .expect("create profile");
    let id = ProfileId::new("work").expect("id");
    {
        let store = PersistentStore::new(&registry);
        store.create_space(&id).expect("create space");
    }
    registry.begin_close(&id).expect("begin close");
    registry.finish_close(&id).expect("finish close");
    (registry, id)
}

#[cfg(unix)]
#[test]
fn destroy_space_refuses_symlink_escape() {
    let root = TestRoot::new();
    let outside = TestRoot::new();
    let sentinel = outside.path.join("sentinel.txt");
    fs::write(&sentinel, b"do-not-touch").expect("sentinel");
    let (registry, id) = closed_registry_with_space(&root, 42);

    // Replace the profile directory with a symlink escape.
    let path = registry.profile_path(&id).expect("path");
    fs::remove_dir_all(&path).expect("remove real dir");
    symlink(&outside.path, &path);

    let store = PersistentStore::new(&registry);
    assert_eq!(
        store.destroy_space(&id),
        Err(PersistentStoreError::GuardRejected)
    );
    assert_eq!(
        fs::read_to_string(&sentinel).expect("sentinel"),
        "do-not-touch"
    );
}

#[cfg(unix)]
#[test]
fn create_space_refuses_symlinked_existing_path() {
    let root = TestRoot::new();
    let outside = TestRoot::new();
    let mut registry = ProfileRegistry::new(root.path.clone()).expect("root");
    registry
        .create_profile_with_directory(
            "work",
            ProfileType::Regular,
            DirectoryId::from_bytes([43; 16]),
        )
        .expect("create profile");
    let id = ProfileId::new("work").expect("id");
    // Pre-plant a symlink where the profile directory would live.
    let path = registry.profile_path(&id).expect("path");
    symlink(&outside.path, &path);

    let store = PersistentStore::new(&registry);
    assert_eq!(
        store.create_space(&id),
        Err(PersistentStoreError::GuardRejected)
    );
}

#[cfg(unix)]
#[test]
fn retry_pending_destroys_skips_escape_staging() {
    let root = TestRoot::new();
    let outside = TestRoot::new();
    let sentinel = outside.path.join("sentinel.txt");
    fs::write(&sentinel, b"do-not-touch").expect("sentinel");
    let registry = ProfileRegistry::new(root.path.clone()).expect("root");
    let store = PersistentStore::new(&registry);

    let dir = DirectoryId::from_bytes([44; 16]).to_hex();
    symlink(&outside.path, &root.path.join(format!("{dir}.deleting-0")));

    // The escape entry is skipped, reported as pending, never followed.
    assert_eq!(store.retry_pending_destroys().expect("resume"), 1);
    assert_eq!(
        fs::read_to_string(&sentinel).expect("sentinel"),
        "do-not-touch"
    );
}

#[test]
fn normal_destroy_and_retry_still_work() {
    let root = TestRoot::new();
    let (registry, id) = closed_registry_with_space(&root, 45);
    let store = PersistentStore::new(&registry);
    assert_eq!(
        store.destroy_space(&id),
        Ok(crayon_profile::DestroyOutcome::Removed)
    );
    // A plain staging directory is still resumed.
    let dir = DirectoryId::from_bytes([46; 16]).to_hex();
    let staging = root.path.join(format!("{dir}.deleting-0"));
    fs::create_dir(&staging).expect("staging");
    fs::write(staging.join("leftover.bin"), b"partial").expect("file");
    assert_eq!(store.retry_pending_destroys().expect("resume"), 0);
    assert!(!staging.exists());
}
