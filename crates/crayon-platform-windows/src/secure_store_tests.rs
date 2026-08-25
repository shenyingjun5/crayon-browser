//! Behaviour tests for the DPAPI secure store.  All cases are
//! deterministic on a real Windows session and isolate state in unique
//! temporary roots.

use super::*;
use crayon_platform_api::secure_store::{SecureStoreError, MAX_VALUE_LEN};
use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};

fn unique_root(tag: &str) -> PathBuf {
    static COUNTER: AtomicU32 = AtomicU32::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    std::env::temp_dir().join(format!("crayon-pltw04-{tag}-{}-{n}", std::process::id()))
}

fn store_at(root: PathBuf) -> DpapiSecureStore {
    DpapiSecureStore::new(root)
}

#[test]
fn roundtrip_store_load() {
    let root = unique_root("roundtrip");
    let mut store = store_at(root.clone());
    let value = b"secret-payload-\xE2\x9C\x93";
    store.store("api.token", value).expect("store");
    // The persisted bytes are DPAPI ciphertext, never the plaintext.
    let on_disk = fs::read(root.join("api.token.bin")).expect("entry file");
    assert!(!on_disk.is_empty());
    assert!(!on_disk.windows(value.len()).any(|window| window == value));
    let loaded = store.load("api.token").expect("load").expect("present");
    assert_eq!(loaded, value);
    let _ = fs::remove_dir_all(&root);
}

#[test]
fn overwrite_last_write_wins() {
    let root = unique_root("overwrite");
    let mut store = store_at(root.clone());
    store.store("k1", b"first").expect("store 1");
    store.store("k1", b"second").expect("store 2");
    assert_eq!(store.load("k1").expect("load").unwrap(), b"second");
    // No temp residue remains after the rename.
    let leftovers: Vec<_> = fs::read_dir(&root)
        .expect("root")
        .filter_map(Result::ok)
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|name| name.contains(".tmp-"))
        .collect();
    assert!(leftovers.is_empty(), "temp files leaked: {leftovers:?}");
    let _ = fs::remove_dir_all(&root);
}

#[test]
fn delete_is_idempotent_and_load_missing_is_none() {
    let root = unique_root("delete");
    let mut store = store_at(root.clone());
    store.delete("absent").expect("idempotent delete");
    store.store("gone", b"x").expect("store");
    store.delete("gone").expect("delete");
    assert_eq!(store.load("gone").expect("load"), None);
}

#[test]
fn corrupted_ciphertext_fails_closed() {
    let root = unique_root("corrupt");
    let mut store = store_at(root.clone());
    store.store("entry", b"value").expect("store");
    let path = root.join("entry.bin");
    fs::write(&path, b"garbage-not-dpapi").expect("overwrite cipher");
    assert_eq!(
        store.load("entry"),
        Err(SecureStoreError::Corrupted),
        "non-DPAPI bytes must not be accepted"
    );
    let _ = fs::remove_dir_all(&root);
}

#[test]
fn oversized_entry_file_is_corrupted_not_parsed() {
    let root = unique_root("oversize");
    let mut store = store_at(root.clone());
    store.store("big", b"v").expect("store");
    let path = root.join("big.bin");
    let huge = vec![0u8; MAX_VALUE_LEN * 4];
    fs::write(&path, &huge).expect("write huge");
    assert_eq!(store.load("big"), Err(SecureStoreError::Corrupted));
    let _ = fs::remove_dir_all(&root);
}

#[test]
fn shape_violations_are_rejected_before_io() {
    let root = unique_root("shape");
    let mut store = store_at(root.clone());
    assert_eq!(
        store.store("", b"v"),
        Err(SecureStoreError::InvalidKey),
        "empty key rejected"
    );
    assert_eq!(
        store.store("../escape", b"v"),
        Err(SecureStoreError::InvalidKey),
        "path-like key rejected"
    );
    let too_big = vec![7u8; MAX_VALUE_LEN + 1];
    assert_eq!(
        store.store("valid-key", &too_big),
        Err(SecureStoreError::ValueTooLarge)
    );
    // Nothing was written for any rejected call.
    assert!(!root.join("escape.bin").exists(), "no traversal file");
    assert!(
        fs::read_dir(&root).is_err(),
        "root must stay untouched on rejection"
    );
}
