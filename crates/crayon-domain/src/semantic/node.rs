//! Semantic node identity and element facts (ACT-01).
//!
//! A `SemanticNodeId` is an opaque, page-session-scoped token minted by the
//! Browser process. It is never a CSS selector, XPath or DOM object; raw
//! DOM/HTML/CDP references are not expressible in this schema.

use crate::semantic::{SemanticSchemaError, MAX_NODE_ID_BYTES, MAX_NODE_NAME_BYTES};
use serde::{Deserialize, Serialize};
use std::fmt::{Display, Formatter};

/// Opaque node identifier: closed charset `[a-z0-9_.:-]`, at most
/// [`MAX_NODE_ID_BYTES`] bytes, minted per navigation generation.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct SemanticNodeId(String);

impl SemanticNodeId {
    /// Validates and wraps an opaque node id token.
    pub fn new(raw: &str) -> Result<Self, SemanticSchemaError> {
        if raw.is_empty() || raw.len() > MAX_NODE_ID_BYTES {
            return Err(SemanticSchemaError::TokenInvalid);
        }
        if !raw
            .bytes()
            .all(|b| matches!(b, b'a'..=b'z' | b'0'..=b'9' | b'_' | b'.' | b':' | b'-'))
        {
            return Err(SemanticSchemaError::TokenInvalid);
        }
        Ok(Self(raw.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl TryFrom<String> for SemanticNodeId {
    type Error = SemanticSchemaError;

    fn try_from(raw: String) -> Result<Self, Self::Error> {
        Self::new(&raw)
    }
}

impl From<SemanticNodeId> for String {
    fn from(id: SemanticNodeId) -> Self {
        id.0
    }
}

impl Display for SemanticNodeId {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Closed v1 element kinds of the semantic map.
///
/// Password and file inputs exist as distinct kinds so the deterministic
/// risk policy (ACT-06) can exclude them from execution; they are never
/// actionable through the v1 action set.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SemanticNodeKind {
    Button,
    Link,
    TextInput,
    PasswordInput,
    FileInput,
    Checkbox,
    Radio,
    Select,
    Slider,
    Textarea,
    Tab,
    MenuItem,
    Heading,
    Text,
    Image,
    Table,
    Form,
    Media,
    Region,
    Other,
}

impl SemanticNodeKind {
    /// All v1 kinds; the closed set locked by golden tests.
    pub const ALL: [Self; 20] = [
        Self::Button,
        Self::Link,
        Self::TextInput,
        Self::PasswordInput,
        Self::FileInput,
        Self::Checkbox,
        Self::Radio,
        Self::Select,
        Self::Slider,
        Self::Textarea,
        Self::Tab,
        Self::MenuItem,
        Self::Heading,
        Self::Text,
        Self::Image,
        Self::Table,
        Self::Form,
        Self::Media,
        Self::Region,
        Self::Other,
    ];

    /// Whether elements of this kind are excluded from action execution by
    /// the frozen v1 policy (credentials and uploads are never actionable).
    #[must_use]
    pub const fn sensitive(self) -> bool {
        matches!(self, Self::PasswordInput | Self::FileInput)
    }
}

/// Observed element state. Absent `Option`s mean "not applicable / not
/// observed" for the kind; values come from verified Browser facts only.
#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ElementState {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default)]
    pub visible: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub checked: Option<bool>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expanded: Option<bool>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub selected: Option<bool>,
}

/// One map node: opaque identity, closed kind, bounded accessible name and
/// the verified element state. No geometry, no selectors, no attributes.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SemanticNode {
    pub id: SemanticNodeId,
    pub kind: SemanticNodeKind,
    pub name: String,
    #[serde(default)]
    pub state: ElementState,
}

impl SemanticNode {
    /// Validates bounds and wraps a node.
    pub fn new(
        id: SemanticNodeId,
        kind: SemanticNodeKind,
        name: String,
        state: ElementState,
    ) -> Result<Self, SemanticSchemaError> {
        if name.len() > MAX_NODE_NAME_BYTES {
            return Err(SemanticSchemaError::BoundExceeded("node name"));
        }
        Ok(Self {
            id,
            kind,
            name,
            state,
        })
    }
}
