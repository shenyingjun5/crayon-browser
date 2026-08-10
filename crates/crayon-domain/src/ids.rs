//! Strongly typed identifiers shared across Crayon product crates.
//!
//! All identifiers crossing the browser/core boundary are validated newtypes:
//! construction rejects empty, over-long, or out-of-charset values instead of
//! propagating raw strings. Wire form is the plain string.

use serde::{de::Error as _, Deserialize, Deserializer, Serialize, Serializer};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum accepted length for any externally supplied identifier.
const MAX_ID_LEN: usize = 128;

/// Identifier validation failure. Stable variants carry no internal detail.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IdError {
    Empty,
    TooLong,
    InvalidCharset,
}

impl Display for IdError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::Empty => "identifier must not be empty",
            Self::TooLong => "identifier exceeds the maximum length",
            Self::InvalidCharset => "identifier contains characters outside [A-Za-z0-9_-]",
        };
        formatter.write_str(message)
    }
}

impl Error for IdError {}

fn validate_id(value: &str) -> Result<&str, IdError> {
    if value.is_empty() {
        return Err(IdError::Empty);
    }
    if value.len() > MAX_ID_LEN {
        return Err(IdError::TooLong);
    }
    if !value
        .bytes()
        .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
    {
        return Err(IdError::InvalidCharset);
    }
    Ok(value)
}

macro_rules! strong_id {
    ($name:ident, $doc:literal) => {
        #[doc = $doc]
        #[derive(Clone, Debug, Eq, Hash, PartialEq)]
        pub struct $name(String);

        impl $name {
            /// Creates a validated identifier.
            pub fn new(value: &str) -> Result<Self, IdError> {
                validate_id(value)?;
                Ok(Self(value.to_owned()))
            }

            #[must_use]
            pub fn as_str(&self) -> &str {
                &self.0
            }
        }

        impl Display for $name {
            fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
                formatter.write_str(&self.0)
            }
        }

        impl Serialize for $name {
            fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                serializer.serialize_str(&self.0)
            }
        }

        impl<'de> Deserialize<'de> for $name {
            fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
                let raw = String::deserialize(deserializer)?;
                Self::new(&raw).map_err(D::Error::custom)
            }
        }
    };
}

strong_id!(
    TabId,
    "Browser tab identifier assigned by the browser process."
);
strong_id!(
    SessionId,
    "Cast/relay session identifier; the secret token itself never appears in wire types."
);
strong_id!(DeviceId, "Paired receiver device identifier.");
strong_id!(
    ResourceId,
    "Opaque media resource identifier within a session (high-entropy, unguessable)."
);

/// Monotonic session generation used to discard stale events.
///
/// A newer generation always wins; events carrying an older generation must be
/// dropped by the receiver (architecture state-machine rule).
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
pub struct SessionGeneration(u64);

impl SessionGeneration {
    pub const INITIAL: Self = Self(0);

    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }

    /// Restores a persisted generation counter (e.g. after process restart).
    #[must_use]
    pub const fn from_raw(value: u64) -> Self {
        Self(value)
    }

    /// Advances to the next generation. Returns `None` on overflow instead of
    /// silently wrapping (u64 overflow is unreachable in practice).
    #[must_use]
    pub fn advance(self) -> Option<Self> {
        self.0.checked_add(1).map(Self)
    }

    /// Reports whether `self` supersedes `older`; stale events return `false`.
    #[must_use]
    pub fn supersedes(self, older: Self) -> bool {
        self > older
    }
}
