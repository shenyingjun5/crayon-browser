//! DPAPI-backed secure storage (PLT-W04a).
//!
//! Values are protected with `CryptProtectData` under the current user
//! scope and persisted as one bounded file per validated key inside the
//! injected root directory.  Plaintext bytes never touch disk; errors are
//! the closed `SecureStoreError` set and never carry key names or value
//! content.

use crate::ffi;
use crayon_platform_api::secure_store::{
    validate_key, validate_value, SecureStore, SecureStoreError, MAX_KEY_LEN, MAX_VALUE_LEN,
};
use std::fs;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};

#[cfg(test)]
#[path = "secure_store_tests.rs"]
mod tests;

/// One DPAPI-protected entry file per key.
const ENTRY_SUFFIX: &str = ".bin";

/// DPAPI secure store rooted at a caller-provided directory.
pub struct DpapiSecureStore {
    root: PathBuf,
}

impl DpapiSecureStore {
    /// Creates a store persisting inside `root`.  The directory is created
    /// on first store; nothing is touched at construction.
    #[must_use]
    pub fn new(root: PathBuf) -> Self {
        Self { root }
    }

    fn entry_path(&self, key: &str) -> PathBuf {
        self.root.join(format!("{key}{ENTRY_SUFFIX}"))
    }

    fn map_io(error: &std::io::Error) -> SecureStoreError {
        match error.kind() {
            ErrorKind::PermissionDenied => SecureStoreError::AccessDenied,
            _ => SecureStoreError::Unavailable,
        }
    }

    /// Atomically replaces the entry file: temp file in the same
    /// directory, then rename over the target.  A failed temp write leaves
    /// no partial state; a failed rename is reported as unavailable.
    fn persist(&self, key: &str, protected: &[u8]) -> Result<(), SecureStoreError> {
        fs::create_dir_all(&self.root).map_err(|e| Self::map_io(&e))?;
        let target = self.entry_path(key);
        let temp = self
            .root
            .join(format!("{key}{ENTRY_SUFFIX}.tmp-{}", std::process::id()));
        fs::write(&temp, protected).map_err(|e| {
            let _ = fs::remove_file(&temp);
            Self::map_io(&e)
        })?;
        fs::rename(&temp, &target)
            .map_err(|e| Self::map_io(&e))
            .inspect_err(|_| {
                let _ = fs::remove_file(&temp);
            })
    }
}

impl SecureStore for DpapiSecureStore {
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        validate_key(key)?;
        validate_value(value)?;
        let protected = ffi::protect(value).ok_or(SecureStoreError::Unavailable)?;
        self.persist(key, &protected)
    }

    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        validate_key(key)?;
        let path = self.entry_path(key);
        if !path.exists() {
            return Ok(None);
        }
        let cipher = read_capped(&path)?;
        let plain = ffi::unprotect(&cipher).ok_or(SecureStoreError::Corrupted)?;
        Ok(Some(plain))
    }

    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        validate_key(key)?;
        let path = self.entry_path(key);
        match fs::remove_file(&path) {
            Ok(()) => Ok(()),
            Err(e) if e.kind() == ErrorKind::NotFound => Ok(()),
            Err(e) => Err(Self::map_io(&e)),
        }
    }
}

/// Reads an entry file with the value bound enforced before unprotecting;
/// oversized or unreadable files fail closed without touching DPAPI.
fn read_capped(path: &Path) -> Result<Vec<u8>, SecureStoreError> {
    let meta = fs::metadata(path).map_err(|e| {
        if e.kind() == ErrorKind::NotFound {
            SecureStoreError::NotFound
        } else {
            DpapiSecureStore::map_io(&e)
        }
    })?;
    let max_cipher = MAX_VALUE_LEN + MAX_KEY_LEN + 256;
    if meta.len() as usize > max_cipher {
        return Err(SecureStoreError::Corrupted);
    }
    fs::read(path).map_err(|e| DpapiSecureStore::map_io(&e))
}
