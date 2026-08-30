//! Semantic map vocabulary (ACT-01, AC-001).
//!
//! The frozen v1 schema shared by the page-data collector, the
//! `crayon-semantic-action` crates and the CAAP tool layer:
//! Page/Action/Form/Media/Risk Map, ChangeSet, effect verification and the
//! stable error codes. Everything here is platform-independent domain data
//! — no CEF/ArkWeb/DOM objects, no selectors, no raw HTML, no field values,
//! no credentials. Raw DOM/HTML/CDP references are structurally
//! inexpressible; extending the closed sets is a protocol-versioned change.
//!
//! Budgets are named constants in this module and enforced by the
//! validating constructors; wire forms deny unknown fields.

mod action;
mod change;
mod effect;
mod error;
mod form;
mod map;
mod media;
mod node;
mod risk;

pub use action::{ActionKind, ActionOffer};
pub use change::ChangeSet;
pub use effect::{EffectOutcome, EffectReason, EffectReport};
pub use error::SemanticError;
pub use form::{FormField, FormMap};
pub use map::{is_valid_origin, PageMap, SemanticTruncation};
pub use media::{MediaElement, MediaKind, MediaState};
pub use node::{ElementState, SemanticNode, SemanticNodeId, SemanticNodeKind};
pub use risk::{RiskEntry, RiskReason};

/// Frozen v1 schema version of the semantic map family.
pub const SEMANTIC_MAP_SCHEMA_VERSION: u32 = 1;

/// Maximum number of nodes in one map or change batch.
pub const MAX_SEMANTIC_NODES: usize = 512;

/// Maximum number of fields in one form map.
pub const MAX_FIELDS_PER_FORM: usize = 64;

/// Maximum bytes of one opaque node id token.
pub const MAX_NODE_ID_BYTES: usize = 64;

/// Maximum bytes of one node accessible name.
pub const MAX_NODE_NAME_BYTES: usize = 256;

/// Maximum bytes of one action summary.
pub const MAX_ACTION_SUMMARY_BYTES: usize = 128;

/// Maximum number of action offers per map.
pub const MAX_ACTION_OFFERS: usize = 256;

/// Maximum number of forms per map.
pub const MAX_FORMS: usize = 16;

/// Maximum bytes of one form field error text.
pub const MAX_FORM_ERROR_BYTES: usize = 256;

/// Maximum number of media elements per map.
pub const MAX_MEDIA_ELEMENTS: usize = 16;

/// Maximum number of risk entries per map.
pub const MAX_RISK_ENTRIES: usize = 512;

/// Maximum number of reasons per risk entry.
pub const MAX_RISK_REASONS: usize = 8;

/// Maximum bytes of one effect detail (never a stack trace or path).
pub const MAX_EFFECT_DETAIL_BYTES: usize = 256;

/// Maximum bytes of one document origin (`scheme://host[:port]`).
pub const MAX_ORIGIN_BYTES: usize = 255;

/// Schema validation failure. Variants are stable and carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SemanticSchemaError {
    /// The carried schema version is not [`SEMANTIC_MAP_SCHEMA_VERSION`].
    VersionMismatch,
    /// A token (node id) failed the closed charset or length check.
    TokenInvalid,
    /// The document origin failed the closed shape check.
    OriginInvalid,
    /// A named budget was exceeded.
    BudgetExceeded(&'static str),
    /// A single field exceeded its length bound.
    BoundExceeded(&'static str),
    /// A unique collection received a duplicate entry.
    DuplicateEntry(&'static str),
    /// A referenced node id does not exist in the map.
    UnknownNode,
    /// An element kind was used where it cannot appear.
    KindMismatch(&'static str),
    /// Revisions are not strictly monotonic.
    RevisionNotMonotonic,
    /// An outcome/reason pairing is not expressible.
    InvalidOutcome,
}

impl std::fmt::Display for SemanticSchemaError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::VersionMismatch => formatter.write_str("schema version mismatch"),
            Self::TokenInvalid => formatter.write_str("token failed the closed charset check"),
            Self::OriginInvalid => formatter.write_str("origin failed the closed shape check"),
            Self::BudgetExceeded(what) => write!(formatter, "budget exceeded: {what}"),
            Self::BoundExceeded(what) => write!(formatter, "bound exceeded: {what}"),
            Self::DuplicateEntry(what) => write!(formatter, "duplicate entry: {what}"),
            Self::UnknownNode => formatter.write_str("reference to an unknown node id"),
            Self::KindMismatch(what) => write!(formatter, "kind mismatch: {what}"),
            Self::RevisionNotMonotonic => formatter.write_str("revisions not monotonic"),
            Self::InvalidOutcome => formatter.write_str("invalid outcome/reason pairing"),
        }
    }
}

impl std::error::Error for SemanticSchemaError {}
