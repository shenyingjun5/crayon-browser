//! Cross-platform secure-store facade (PRV-05).
//!
//! Wraps the PLT-01 `SecureStore` trait behind a unified interface so
//! the product assembly can inject the platform backend (Windows DPAPI
//! from `crayon-platform-windows` or macOS Keychain from
//! `crayon-platform-macos`) without the rest of the product knowing
//! which platform it runs on.  The facade performs key validation and
//! delegates all operations to the injected backend; it never touches
//! the keychain or DPAPI itself.

use crayon_platform_api::secure_store::{
    validate_key, validate_value, SecureStore, SecureStoreError,
};

#[cfg(test)]
#[path = "secure_store_tests.rs"]
mod tests;

/// Cross-platform secure-store facade.  The platform backend is
/// injected as a trait object; the facade adds key validation and a
/// uniform error surface.
pub struct SecureStoreFacade {
    backend: Box<dyn SecureStore + Send>,
}

impl SecureStoreFacade {
    /// Creates a facade over the given platform backend.
    pub fn new(backend: Box<dyn SecureStore + Send>) -> Self {
        Self { backend }
    }

    /// Stores `value` under `key`, replacing any previous entry
    /// (rotation = store-overwrite; the platform backend guarantees
    /// atomicity).
    pub fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        validate_key(key)?;
        validate_value(value)?;
        self.backend.store(key, value)
    }

    /// Loads the value for `key`; `Ok(None)` when absent.
    pub fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        validate_key(key)?;
        self.backend.load(key)
    }

    /// Deletes `key`; deleting an absent key succeeds (idempotent).
    pub fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        validate_key(key)?;
        self.backend.delete(key)
    }

    /// Rotates a key: stores the new value and verifies the roundtrip.
    /// Returns an error if the write did not stick.
    pub fn rotate(&mut self, key: &str, new_value: &[u8]) -> Result<(), SecureStoreError> {
        self.store(key, new_value)?;
        // Verify the write landed (defense in depth).
        match self.backend.load(key)? {
            Some(stored) if stored == new_value => Ok(()),
            _ => Err(SecureStoreError::Corrupted),
        }
    }

    /// Reports whether the key is valid without touching the backend.
    pub fn validate_key_shape(key: &str) -> Result<(), SecureStoreError> {
        validate_key(key)
    }
}

/// Platform secure-store backend kind (diagnostics only).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SecureStoreBackendKind {
    Dpapi,
    Keychain,
}

/// Reports the current platform's backend kind (compile-time).
#[must_use]
pub const fn platform_backend() -> SecureStoreBackendKind {
    if cfg!(target_os = "macos") {
        SecureStoreBackendKind::Keychain
    } else {
        SecureStoreBackendKind::Dpapi
    }
}
