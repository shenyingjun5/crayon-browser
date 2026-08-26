//! PRV-05 secure-store facade tests: CRUD matrix via a fake backend,
//! rotation, key validation, unavailable passthrough.

use super::*;
use crayon_platform_api::secure_store::SecureStoreError;

struct FakeBackend {
    entries: std::collections::HashMap<String, Vec<u8>>,
}

impl SecureStore for FakeBackend {
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        self.entries.insert(key.to_string(), value.to_vec());
        Ok(())
    }
    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        Ok(self.entries.get(key).cloned())
    }
    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        self.entries.remove(key);
        Ok(())
    }
}

struct UnavailableBackend;

impl SecureStore for UnavailableBackend {
    fn store(&mut self, _key: &str, _value: &[u8]) -> Result<(), SecureStoreError> {
        Err(SecureStoreError::Unavailable)
    }
    fn load(&self, _key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        Err(SecureStoreError::Unavailable)
    }
    fn delete(&mut self, _key: &str) -> Result<(), SecureStoreError> {
        Err(SecureStoreError::Unavailable)
    }
}

#[test]
fn crud_matrix() {
    let mut facade = SecureStoreFacade::new(Box::new(FakeBackend {
        entries: std::collections::HashMap::new(),
    }));
    assert_eq!(facade.load("key-a"), Ok(None));
    facade.store("key-a", b"secret-1").unwrap();
    assert_eq!(facade.load("key-a"), Ok(Some(b"secret-1".to_vec())));
    facade.store("key-a", b"secret-2").unwrap();
    assert_eq!(facade.load("key-a"), Ok(Some(b"secret-2".to_vec())));
    facade.delete("key-a").unwrap();
    assert_eq!(facade.load("key-a"), Ok(None));
    facade.delete("key-a").unwrap();
    facade.store("key-b", b"val-b").unwrap();
    facade.store("key-c", b"val-c").unwrap();
    assert_eq!(facade.load("key-b"), Ok(Some(b"val-b".to_vec())));
    assert_eq!(facade.load("key-c"), Ok(Some(b"val-c".to_vec())));
}

#[test]
fn rotate_verifies_roundtrip() {
    let mut facade = SecureStoreFacade::new(Box::new(FakeBackend {
        entries: std::collections::HashMap::new(),
    }));
    facade.rotate("key-r", b"v1").unwrap();
    facade.rotate("key-r", b"v2").unwrap();
    assert_eq!(facade.load("key-r"), Ok(Some(b"v2".to_vec())));
}

#[test]
fn key_validation_fails_closed() {
    let mut facade = SecureStoreFacade::new(Box::new(FakeBackend {
        entries: std::collections::HashMap::new(),
    }));
    assert_eq!(facade.store("", b"x"), Err(SecureStoreError::InvalidKey));
    assert_eq!(
        facade.store("bad key", b"x"),
        Err(SecureStoreError::InvalidKey)
    );
    assert_eq!(facade.load("bad key"), Err(SecureStoreError::InvalidKey));
    assert_eq!(facade.delete("bad key"), Err(SecureStoreError::InvalidKey));
    assert!(SecureStoreFacade::validate_key_shape("ok-key").is_ok());
    assert!(SecureStoreFacade::validate_key_shape("bad key").is_err());
}

#[test]
fn unavailable_passthrough() {
    let mut facade = SecureStoreFacade::new(Box::new(UnavailableBackend));
    assert_eq!(facade.store("k", b"v"), Err(SecureStoreError::Unavailable));
    assert_eq!(facade.load("k"), Err(SecureStoreError::Unavailable));
    assert_eq!(facade.delete("k"), Err(SecureStoreError::Unavailable));
    assert_eq!(facade.rotate("k", b"v"), Err(SecureStoreError::Unavailable));
}

#[test]
fn backend_kind_is_compile_time() {
    let kind = platform_backend();
    assert!(matches!(
        kind,
        SecureStoreBackendKind::Keychain | SecureStoreBackendKind::Dpapi
    ));
}
