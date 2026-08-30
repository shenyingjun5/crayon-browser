//! Action handle identity (ACT-03).
//!
//! An `ActionHandleId` is an opaque, high-entropy, one-page-session token
//! minted by the Browser process. It is not a selector, not a DOM
//! reference and not a persistent identifier: it dies with its TTL, its
//! page generation or its single consumption, whichever comes first.

use serde::{Deserialize, Serialize};
use std::fmt::{Display, Formatter};

/// Random bytes entropy of one minted handle id (128-bit).
const ID_ENTROPY_BYTES: usize = 16;

/// Closed base32 alphabet (RFC 4648, lowercase); keeps the wire token
/// inside `[a-z2-7]` with no padding and no separators.
const BASE32_ALPHABET: &[u8; 32] = b"abcdefghijklmnopqrstuvwxyz234567";

/// Handle id token prefix; keeps handle ids visually distinct from node ids.
const ID_PREFIX: char = 'h';

/// A handle id is `h` + 26 base32 chars (128-bit entropy), so exactly
/// [`ID_LEN`] bytes inside the closed charset `[a-z2-7]`.
pub const ID_LEN: usize = 1 + (ID_ENTROPY_BYTES * 8).div_ceil(5);

/// Invalid handle token.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandleIdError {
    /// Wrong length or charset; never a valid token.
    Invalid,
}

impl Display for HandleIdError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("handle id must be 'h' plus 27 lowercase base32 chars")
    }
}

impl std::error::Error for HandleIdError {}

/// Opaque action handle identifier: high entropy, single generation, never
/// persisted and never derived from page content.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct ActionHandleId(String);

impl ActionHandleId {
    /// Wraps an externally supplied token after full validation.
    pub fn new(raw: &str) -> Result<Self, HandleIdError> {
        if raw.len() != ID_LEN
            || !raw.starts_with(ID_PREFIX)
            || !raw
                .bytes()
                .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit())
        {
            return Err(HandleIdError::Invalid);
        }
        Ok(Self(raw.to_owned()))
    }

    /// Mints a fresh high-entropy id from the OS entropy source.
    pub fn generate() -> Result<Self, HandleIdError> {
        let mut bytes = [0u8; ID_ENTROPY_BYTES];
        getrandom::fill(&mut bytes).map_err(|_| HandleIdError::Invalid)?;
        let mut token = String::with_capacity(ID_LEN);
        token.push(ID_PREFIX);
        let mut buffer = 0u32;
        let mut bits = 0u32;
        for byte in bytes {
            buffer = (buffer << 8) | u32::from(byte);
            bits += 8;
            while bits >= 5 {
                bits -= 5;
                let index = ((buffer >> bits) & 0x1f) as usize;
                token.push(BASE32_ALPHABET[index] as char);
            }
        }
        if bits > 0 {
            let index = ((buffer << (5 - bits)) & 0x1f) as usize;
            token.push(BASE32_ALPHABET[index] as char);
        }
        Self::new(&token)
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl TryFrom<String> for ActionHandleId {
    type Error = HandleIdError;

    fn try_from(raw: String) -> Result<Self, Self::Error> {
        Self::new(&raw)
    }
}

impl From<ActionHandleId> for String {
    fn from(id: ActionHandleId) -> Self {
        id.0
    }
}

impl Display for ActionHandleId {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}
