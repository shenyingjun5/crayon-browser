//! Secure storage interface (Windows DPAPI / macOS Keychain).
//!
//! Values are opaque byte strings bounded to 4 KiB; keys are closed-charset
//! tokens.  Errors never reveal key material or user data.

use crate::token::validate_token;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum length of a secure-store key, in bytes.
pub const MAX_KEY_LEN: usize = 64;

/// Maximum size of a stored value, in bytes.
pub const MAX_VALUE_LEN: usize = 4096;

/// Secure-store operation failure.  Variants are stable and carry no key
/// material or user data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SecureStoreError {
    /// The platform facility is unavailable (locked, disabled, missing).
    Unavailable,
    /// The OS denied access to the secure store.
    AccessDenied,
    /// The key does not exist.
    NotFound,
    /// Stored data failed integrity or schema checks.
    Corrupted,
    /// The key violates shape or bounds.
    InvalidKey,
    /// The value exceeds the size bound.
    ValueTooLarge,
}

impl Display for SecureStoreError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::Unavailable => "secure store is unavailable",
            Self::AccessDenied => "secure store access denied",
            Self::NotFound => "secure store entry not found",
            Self::Corrupted => "secure store entry corrupted",
            Self::InvalidKey => "secure store key rejected",
            Self::ValueTooLarge => "secure store value exceeds size limit",
        };
        formatter.write_str(message)
    }
}

impl Error for SecureStoreError {}

/// Validates a secure-store key against the closed token charset.
pub fn validate_key(key: &str) -> Result<(), SecureStoreError> {
    validate_token(key, MAX_KEY_LEN).map_err(SecureStoreError::from)
}

/// Validates a value against the size bound.
pub fn validate_value(value: &[u8]) -> Result<(), SecureStoreError> {
    if value.len() > MAX_VALUE_LEN {
        return Err(SecureStoreError::ValueTooLarge);
    }
    Ok(())
}

/// OS-backed secure storage.  Implementations live in `platform/*`; the
/// browser core never talks to DPAPI or Keychain directly.
pub trait SecureStore: Send {
    /// Stores or replaces `value` under `key`.
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError>;

    /// Loads the value for `key`; `Ok(None)` when absent.
    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError>;

    /// Deletes `key`; deleting an absent key succeeds (idempotent).
    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError>;
}

#[cfg(test)]
#[path = "secure_store_tests.rs"]
mod tests;
