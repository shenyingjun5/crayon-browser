//! PlatformFake self-tests: capability fixtures and secure-store behaviour
//! (overwrite, missing delete, injected write failure, capacity bound).

use crayon_domain::{BrowserEngineKind, LocalDiscoveryKind, ProtectedSurfaceKind, SecureStoreKind};
use test_support::platform::{PlatformFake, SecureStoreError, SecureStoreFake};

#[test]
fn cef_desktop_fixture_matches_the_design_example() {
    let caps = PlatformFake::cef_desktop();
    assert_eq!(caps.browser_engine(), BrowserEngineKind::Cef);
    assert!(caps.tab_video());
    assert!(caps.system_audio());
    assert!(caps.hardware_h264());
    assert_eq!(caps.local_discovery(), LocalDiscoveryKind::MdnsUdp);
    assert_eq!(caps.secure_store(), SecureStoreKind::OsNative);
    assert_eq!(caps.protected_surface(), ProtectedSurfaceKind::Blocked);
}

#[test]
fn arkweb_fixture_is_explicitly_reduced() {
    let caps = PlatformFake::arkweb_reduced();
    assert_eq!(caps.browser_engine(), BrowserEngineKind::ArkWeb);
    assert!(!caps.tab_video());
    assert!(!caps.system_audio());
    assert_eq!(caps.local_discovery(), LocalDiscoveryKind::Unavailable);
}

#[test]
fn secure_store_put_get_delete() {
    let store = SecureStoreFake::new();
    assert!(store.is_empty());
    store.put("device-key", b"fake-bytes").unwrap();
    store.put("device-key", b"newer").unwrap(); // overwrite replaces
    assert_eq!(
        store.get("device-key").as_deref(),
        Some(b"newer".as_slice())
    );
    assert_eq!(store.len(), 1);

    store.delete("device-key").unwrap();
    assert!(store.get("device-key").is_none());
    assert_eq!(
        store.delete("device-key"),
        Err(SecureStoreError::KeyNotFound)
    );
}

#[test]
fn secure_store_injected_failure_is_consumed_once() {
    let store = SecureStoreFake::new();
    store.fail_next_write(SecureStoreError::Unavailable);
    assert_eq!(store.put("k", b"v"), Err(SecureStoreError::Unavailable));
    assert_eq!(store.put("k", b"v"), Ok(()));
}

#[test]
fn secure_store_is_bounded() {
    let store = SecureStoreFake::new();
    assert_eq!(
        store.put("oversized", &vec![0u8; 4097]),
        Err(SecureStoreError::CapacityExceeded)
    );
    for i in 0..128 {
        store.put(&format!("key-{i:03}"), b"v").unwrap();
    }
    assert_eq!(
        store.put("key-129", b"v"),
        Err(SecureStoreError::CapacityExceeded)
    );
    // Replacing an existing key still works at capacity.
    store.put("key-000", b"v2").unwrap();
}
