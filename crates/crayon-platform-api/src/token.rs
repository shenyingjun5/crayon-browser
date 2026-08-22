//! Closed-charset token validation shared by externally supplied identifiers
//! (secure-store keys, network interface names). Tokens are ASCII
//! alphanumeric plus `-`, `_`, `.`; length is bounded per call site.

use crate::secure_store::SecureStoreError;

/// Characters permitted inside a validated token.
const ALLOWED_EXTRA: &[char] = &['-', '_', '.'];

/// Validates `value` against the closed token charset with the given
/// maximum length. Empty tokens are rejected.
pub fn validate_token(value: &str, max_len: usize) -> Result<(), TokenError> {
    if value.is_empty() {
        return Err(TokenError::Empty);
    }
    if value.len() > max_len {
        return Err(TokenError::TooLong);
    }
    if !value
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || ALLOWED_EXTRA.contains(&c))
    {
        return Err(TokenError::InvalidCharacter);
    }
    Ok(())
}

/// Closed validation failure for external tokens.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TokenError {
    Empty,
    TooLong,
    InvalidCharacter,
}

impl std::fmt::Display for TokenError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let text = match self {
            Self::Empty => "token is empty",
            Self::TooLong => "token exceeds maximum length",
            Self::InvalidCharacter => "token contains a disallowed character",
        };
        f.write_str(text)
    }
}

impl std::error::Error for TokenError {}

impl From<TokenError> for SecureStoreError {
    fn from(_: TokenError) -> Self {
        Self::InvalidKey
    }
}

#[cfg(test)]
#[path = "token_tests.rs"]
mod tests;
