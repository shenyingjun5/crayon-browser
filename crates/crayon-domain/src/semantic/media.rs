//! Media map schema (ACT-01).
//!
//! Media facts mirror the verified observation vocabulary of the media
//! policy layer; the semantic map carries no stream URLs and no DRM state
//! beyond the closed refusal kinds.

use crate::semantic::node::SemanticNodeId;
use crate::semantic::{SemanticSchemaError, MAX_MEDIA_ELEMENTS};
use serde::{Deserialize, Serialize};

/// Closed media element kinds.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MediaKind {
    Video,
    Audio,
}

impl MediaKind {
    /// All v1 kinds; the closed set locked by golden tests.
    pub const ALL: [Self; 2] = [Self::Video, Self::Audio];
}

/// Closed playback states observable through verified Browser facts.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum MediaState {
    Playing,
    Paused,
    Ended,
    Buffering,
    Unknown,
}

impl MediaState {
    /// All v1 states; the closed set locked by golden tests.
    pub const ALL: [Self; 5] = [
        Self::Playing,
        Self::Paused,
        Self::Ended,
        Self::Buffering,
        Self::Unknown,
    ];
}

/// One media element with its observed playback state. Playback control
/// stays behind the cast/media gates; this map is read-only vocabulary.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct MediaElement {
    pub node: SemanticNodeId,
    pub kind: MediaKind,
    pub state: MediaState,
    #[serde(default)]
    pub muted: bool,
    #[serde(default)]
    pub has_controls: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub duration_ms: Option<u64>,
}

impl MediaElement {
    /// Validates bounds and wraps a media element.
    pub fn new(
        node: SemanticNodeId,
        kind: MediaKind,
        state: MediaState,
        muted: bool,
        has_controls: bool,
        duration_ms: Option<u64>,
    ) -> Result<Self, SemanticSchemaError> {
        Ok(Self {
            node,
            kind,
            state,
            muted,
            has_controls,
            duration_ms,
        })
    }
}

/// Bounds check helper for the map assembly.
pub(crate) fn validate_media(media: &[MediaElement]) -> Result<(), SemanticSchemaError> {
    if media.len() > MAX_MEDIA_ELEMENTS {
        return Err(SemanticSchemaError::BudgetExceeded("media elements"));
    }
    Ok(())
}
