//! M04a keychain secure-store tests.
//!
//! These tests touch the real login keychain under the production
//! service namespace with dedicated probe keys, cleaning before and
//! after each test so runs stay hermetic even after failures.  Items created and read by the same process do
//! not prompt (AGENTS.md keychain decision: the keychain is touched
//! only when a user action actually stores/reads a secret — a test run
//! is such an action).

use super::*;
use crayon_platform_api::secure_store::{SecureStore, MAX_KEY_LEN, MAX_VALUE_LEN};

const PROBE_KEYS: &[&str] = &["probe-a", "probe-b", "probe-c", "probe-dyn"];

fn test_store() -> KeychainSecureStore {
    // Hermetic start: remove any items left by earlier failed runs.
    clean_probe_keys();
    KeychainSecureStore::new()
}

fn clean_probe_keys() {
    // Per-key removal, then a service-wide sweep that also catches
    // items left by earlier buggy runs (e.g. NULL-account items a
    // keyed delete can never match).
    for key in PROBE_KEYS {
        let _ = ffi::sec_delete(SERVICE, key.as_bytes());
    }
    let _ = ffi::sec_delete_service_all(SERVICE);
}

#[test]
fn keychain_roundtrip_matrix() {
    let mut store = test_store();
    let key = "probe-a";

    // Absent key loads as None.
    assert_eq!(store.load(key), Ok(None));

    // Store → load roundtrip.
    store.store(key, b"secret-bytes").expect("store");
    assert_eq!(store.load(key), Ok(Some(b"secret-bytes".to_vec())));

    // Overwrite replaces.
    store.store(key, b"second").expect("overwrite");
    assert_eq!(store.load(key), Ok(Some(b"second".to_vec())));

    // Delete is idempotent: first Ok, second Ok (absent).
    store.delete(key).expect("delete");
    assert_eq!(store.load(key), Ok(None));
    store.delete(key).expect("delete absent");

    // Empty value is legal (bounded, zero-length).
    store.store(key, b"").expect("empty value");
    assert_eq!(store.load(key), Ok(Some(Vec::new())));

    clean_probe_keys();
}

#[test]
fn keychain_validation_fails_closed() {
    let mut store = test_store();
    // Invalid key shapes never reach the keychain.
    assert_eq!(store.store("", b"x"), Err(SecureStoreError::InvalidKey));
    assert_eq!(
        store.store("bad key", b"x"),
        Err(SecureStoreError::InvalidKey)
    );
    assert_eq!(
        store.store(&"k".repeat(MAX_KEY_LEN + 1), b"x"),
        Err(SecureStoreError::InvalidKey)
    );
    assert_eq!(
        store.store("probe-b", &vec![0u8; MAX_VALUE_LEN + 1]),
        Err(SecureStoreError::ValueTooLarge)
    );
    assert_eq!(store.load("bad key"), Err(SecureStoreError::InvalidKey));
    assert_eq!(store.delete("bad key"), Err(SecureStoreError::InvalidKey));
    clean_probe_keys();
}

#[test]
fn keychain_multiple_keys_independent() {
    let mut store = test_store();
    store.store("probe-b", b"value-b").expect("store b");
    store.store("probe-c", b"value-c").expect("store c");
    assert_eq!(store.load("probe-b"), Ok(Some(b"value-b".to_vec())));
    assert_eq!(store.load("probe-c"), Ok(Some(b"value-c".to_vec())));
    store.delete("probe-b").expect("delete b");
    assert_eq!(store.load("probe-b"), Ok(None));
    assert_eq!(store.load("probe-c"), Ok(Some(b"value-c".to_vec())));
    clean_probe_keys();
}

#[test]
fn object_safety_assertion() {
    // Platform adapters must be usable as `dyn SecureStore`.
    let key = "probe-dyn";
    let mut store: Box<dyn SecureStore> = Box::new(KeychainSecureStore::new());
    store.store(key, b"dyn").expect("store");
    assert_eq!(store.load(key), Ok(Some(b"dyn".to_vec())));
    store.delete(key).expect("delete");
}
