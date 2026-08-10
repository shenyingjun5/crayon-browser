//! Frozen Core API v1 wire messages (FND-08).
//!
//! Wire contract rules:
//! - every struct uses `deny_unknown_fields`; unknown keys are rejected;
//! - no UI copy, no OS/Cast-SDK internal types, no secrets (see `secret` module);
//! - page/media URLs are in-memory policy inputs only: they must never be
//!   logged, persisted, or forwarded to receivers or the cloud (design §15).

use crayon_domain::{CoreError, ReceiverCapabilities, TabId};
use serde::{Deserialize, Serialize};

/// Media container protocol of a candidate stream.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProtocolKind {
    Hls,
    Dash,
    Mp4,
}

/// Request-header requirements of the upstream media (§11.3).
///
/// P0 direct cast only allows credential-free classes; media bound to
/// Cookie/Authorization degrades to tab mirroring.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HeadersClass {
    None,
    RefererOnly,
    RefererAndUa,
    CredentialBound,
}

/// Ad-continuity state (§9.3). Deliberately has no `skippable`/`ad_free`
/// variants so the schema cannot drive ad-avoidance behaviour.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AdContinuity {
    Preserved,
    NotApplicable,
    Unknown,
}

/// Video codec of a candidate, when probed.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VideoCodecKind {
    H264,
    Hevc,
    Av1,
    Vp9,
    Vp8,
    Other,
}

/// Audio codec of a candidate, when probed.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AudioCodecKind {
    Aac,
    Opus,
    Eac3,
    Other,
}

/// Verified page context attached to an observation.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PageContext {
    tab_id: TabId,
    /// Full top-level page URL. Policy input only — never logged or forwarded.
    url: String,
}

impl PageContext {
    #[must_use]
    pub fn new(tab_id: TabId, url: String) -> Self {
        Self { tab_id, url }
    }

    #[must_use]
    pub fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    #[must_use]
    pub fn url(&self) -> &str {
        &self.url
    }
}

/// Playback state at observation time (§9.1 `playback`).
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PlaybackState {
    position_seconds: f64,
    duration_seconds: Option<f64>,
    is_live: bool,
}

impl PlaybackState {
    #[must_use]
    pub const fn new(position_seconds: f64, duration_seconds: Option<f64>, is_live: bool) -> Self {
        Self {
            position_seconds,
            duration_seconds,
            is_live,
        }
    }

    #[must_use]
    pub const fn position_seconds(self) -> f64 {
        self.position_seconds
    }

    #[must_use]
    pub const fn duration_seconds(self) -> Option<f64> {
        self.duration_seconds
    }

    #[must_use]
    pub const fn is_live(self) -> bool {
        self.is_live
    }
}

/// Browser-verified observation of the current page's playback (§8).
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SourceObservation {
    page: PageContext,
    playback: PlaybackState,
}

impl SourceObservation {
    #[must_use]
    pub fn new(page: PageContext, playback: PlaybackState) -> Self {
        Self { page, playback }
    }

    #[must_use]
    pub const fn page(&self) -> &PageContext {
        &self.page
    }

    #[must_use]
    pub const fn playback(&self) -> PlaybackState {
        self.playback
    }
}

/// Normalized media candidate (§9.1 `candidate`).
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct MediaCandidate {
    /// Upstream media URL. Policy input only — never logged or forwarded.
    url: String,
    protocol: ProtocolKind,
    drm: bool,
    headers_class: HeadersClass,
    video_codec: Option<VideoCodecKind>,
    audio_codec: Option<AudioCodecKind>,
    ad_continuity: AdContinuity,
}

impl MediaCandidate {
    #[must_use]
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        url: String,
        protocol: ProtocolKind,
        drm: bool,
        headers_class: HeadersClass,
        video_codec: Option<VideoCodecKind>,
        audio_codec: Option<AudioCodecKind>,
        ad_continuity: AdContinuity,
    ) -> Self {
        Self {
            url,
            protocol,
            drm,
            headers_class,
            video_codec,
            audio_codec,
            ad_continuity,
        }
    }

    #[must_use]
    pub fn url(&self) -> &str {
        &self.url
    }

    #[must_use]
    pub const fn protocol(&self) -> ProtocolKind {
        self.protocol
    }

    #[must_use]
    pub const fn drm(&self) -> bool {
        self.drm
    }

    #[must_use]
    pub const fn headers_class(&self) -> HeadersClass {
        self.headers_class
    }

    #[must_use]
    pub const fn video_codec(&self) -> Option<VideoCodecKind> {
        self.video_codec
    }

    #[must_use]
    pub const fn audio_codec(&self) -> Option<AudioCodecKind> {
        self.audio_codec
    }

    #[must_use]
    pub const fn ad_continuity(&self) -> AdContinuity {
        self.ad_continuity
    }
}

/// Complete policy-engine input (§9.1).
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CastPolicyInput {
    page: PageContext,
    playback: PlaybackState,
    candidate: MediaCandidate,
    receiver: ReceiverCapabilities,
}

impl CastPolicyInput {
    #[must_use]
    pub fn new(
        page: PageContext,
        playback: PlaybackState,
        candidate: MediaCandidate,
        receiver: ReceiverCapabilities,
    ) -> Self {
        Self {
            page,
            playback,
            candidate,
            receiver,
        }
    }

    #[must_use]
    pub const fn page(&self) -> &PageContext {
        &self.page
    }

    #[must_use]
    pub const fn playback(&self) -> PlaybackState {
        self.playback
    }

    #[must_use]
    pub const fn candidate(&self) -> &MediaCandidate {
        &self.candidate
    }

    #[must_use]
    pub const fn receiver(&self) -> ReceiverCapabilities {
        self.receiver
    }
}

/// Policy-engine decision (§9.2). `Reject` always carries a stable
/// `CoreError` code instead of a natural-language reason.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "decision", rename_all = "snake_case")]
#[serde(deny_unknown_fields)]
pub enum CastPolicyDecision {
    /// Tab capture + WebRTC mirroring.
    Mirror,
    /// Receiver pulls the stream directly through the session relay.
    Direct,
    Reject {
        reason: CoreError,
    },
}
