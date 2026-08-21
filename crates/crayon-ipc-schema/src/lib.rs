//! Version negotiation primitives and frozen v1 wire messages for
//! browser/core communication, plus the CAAP v1 agent envelope set.

mod caap;
mod messages;
mod secret;

pub use caap::{
    CaapCancel, CaapChunk, CaapErrorReply, CaapHello, CaapRequest, CaapSchemaError, CaapWelcome,
    MAX_CAAP_CAPABILITIES, MAX_CAAP_CHUNK_BYTES, MAX_CAAP_PARAMS, MAX_CAAP_PARAM_KEY_LEN,
    MAX_CAAP_PARAM_VALUE_LEN, MAX_CAAP_TOKEN_LEN,
};
pub use messages::{
    AdContinuity, AudioCodecKind, CastPolicyDecision, CastPolicyInput, ExternalClientHandoff,
    HandoffConfirmation, HandoffReason, HeadersClass, MediaCandidate, PageContext, PlaybackState,
    ProtocolKind, SourceObservation, VideoCodecKind,
};
pub use secret::{SessionGrant, SessionSecret};

use crayon_domain::ProductMode;
use serde::{de::Error as _, Deserialize, Deserializer, Serialize, Serializer};
use std::num::NonZeroU16;

/// Non-zero IPC schema version. Wire form is the plain `u16`; zero is
/// rejected at deserialization.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct SchemaVersion(NonZeroU16);

impl SchemaVersion {
    pub const CURRENT: Self = Self(NonZeroU16::MIN);

    #[must_use]
    pub const fn new(value: NonZeroU16) -> Self {
        Self(value)
    }

    #[must_use]
    pub const fn get(self) -> u16 {
        self.0.get()
    }

    /// Uses exact-version negotiation until the v1 compatibility window is frozen.
    #[must_use]
    pub const fn is_supported_by(self, peer: Self) -> bool {
        self.get() == peer.get()
    }
}

impl Serialize for SchemaVersion {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_u16(self.get())
    }
}

impl<'de> Deserialize<'de> for SchemaVersion {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let raw = u16::deserialize(deserializer)?;
        NonZeroU16::new(raw)
            .map(Self)
            .ok_or_else(|| D::Error::custom("schema_version must be non-zero"))
    }
}

/// Minimal startup negotiation data shared by browser and core.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Handshake {
    schema_version: SchemaVersion,
    product_mode: ProductMode,
}

impl Handshake {
    #[must_use]
    pub const fn current(product_mode: ProductMode) -> Self {
        Self {
            schema_version: SchemaVersion::CURRENT,
            product_mode,
        }
    }

    #[must_use]
    pub const fn schema_version(self) -> SchemaVersion {
        self.schema_version
    }

    #[must_use]
    pub const fn product_mode(self) -> ProductMode {
        self.product_mode
    }
}
