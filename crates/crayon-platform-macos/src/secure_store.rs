//! Keychain-backed secure storage (PLT-M04a).
//!
//! Values are stored as generic-password items under the crayon
//! service namespace with `kSecAttrAccessibleAfterFirstUnlock`.  The
//! keychain is touched only when the user actually stores, reads or
//! deletes a secret (AGENTS.md project memory, 2026-08-23 decision) —
//! never at launch.  Errors are the closed `SecureStoreError` set and
//! never carry key names or value content.

use crate::ffi;
use crayon_platform_api::secure_store::{
    validate_key, validate_value, SecureStore, SecureStoreError,
};

#[cfg(test)]
#[path = "secure_store_tests.rs"]
mod tests;

/// Service namespace for all viewer secure-store items.
const SERVICE: &str = "com.crayon.browser.secure-store";

/// Creates a store with a specific service namespace.
#[cfg(test)]
pub(crate) fn new_with_service(service: &'static str) -> KeychainSecureStore {
    KeychainSecureStore { service }
}

/// Maps a raw Security status to the closed error set.
fn map_status(status: i32) -> SecureStoreError {
    match status {
        ffi::ERR_SEC_ITEM_NOT_FOUND => SecureStoreError::NotFound,
        ffi::ERR_SEC_AUTH_FAILED | ffi::ERR_SEC_INTERACTION_NOT_ALLOWED => {
            SecureStoreError::AccessDenied
        }
        ffi::ERR_SEC_ACCESS_DENIED => SecureStoreError::AccessDenied,
        _ => SecureStoreError::Unavailable,
    }
}

/// Keychain secure store.  All operations are synchronous and touch
/// the login keychain only when invoked.
/// Keychain secure store.  All operations are synchronous and touch
/// the login keychain only when invoked.
pub struct KeychainSecureStore {
    service: &'static str,
}

impl Default for KeychainSecureStore {
    fn default() -> Self {
        Self { service: SERVICE }
    }
}

impl KeychainSecureStore {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }
}

impl SecureStore for KeychainSecureStore {
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        validate_key(key)?;
        validate_value(value)?;
        // Idempotent clear first: SecItemUpdate would need the same
        // query anyway, and delete+add keeps the ACL anchored to this
        // process.
        let delete_status = ffi::sec_delete(self.service, key.as_bytes());
        if delete_status != ffi::ERR_SEC_SUCCESS && delete_status != ffi::ERR_SEC_ITEM_NOT_FOUND {
            return Err(map_status(delete_status));
        }
        let status = ffi::sec_add(self.service, key.as_bytes(), value);
        if status == ffi::ERR_SEC_SUCCESS {
            Ok(())
        } else {
            Err(map_status(status))
        }
    }

    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        validate_key(key)?;
        let (status, data) = ffi::sec_copy(self.service, key.as_bytes());
        if status == ffi::ERR_SEC_SUCCESS {
            Ok(data.map(|d| d.bytes().to_vec()))
        } else if status == ffi::ERR_SEC_ITEM_NOT_FOUND {
            Ok(None) // absent key: contract says Ok(None), not an error
        } else {
            Err(map_status(status))
        }
    }

    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        validate_key(key)?;
        let status = ffi::sec_delete(self.service, key.as_bytes());
        if status == ffi::ERR_SEC_SUCCESS || status == ffi::ERR_SEC_ITEM_NOT_FOUND {
            Ok(())
        } else {
            Err(map_status(status))
        }
    }
}
