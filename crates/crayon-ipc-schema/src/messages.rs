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
/// Cookie/Authorization degrades to an external-client handoff suggestion.
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

/// Why Direct/Relay are unavailable and an external-client handoff is
/// suggested instead (MED-19). Stable machine-readable reason, never UI copy.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HandoffReason {
    /// Encrypted/key-required stream: the current compliance posture keeps
    /// the media inside the browser (PL-005).
    KeyRequired,
    /// blob:/MediaStream source has no castable URL (BR-012).
    NoDirectUrl,
    /// Probe was inconclusive: safe fallback only (PL-014).
    ProbeInconclusive,
    /// Media is bound to Cookie/Authorization (PL-008).
    CredentialBound,
    /// Receiver cannot play the candidate protocol/codec (PL-007).
    ReceiverIncompatible,
    /// Ad continuity unknown with from-the-start playback (PL-009).
    AdContinuityUnknown,
    /// Direct/Relay start failed at runtime; single-step downgrade
    /// (design §9.2 step 7).
    StartFailed,
    /// DASH relay serving is out of v1 scope (documented v1 limit).
    DashRelayUnsupported,
    /// Legacy v1 `mirror` decision read through the compatibility window;
    /// the old wire carried no reason. Never produced by new decisions.
    #[default]
    LegacyMirror,
}

/// User-confirmation requirement attached to every handoff (PL-015).
///
/// Single variant on purpose: a handoff suggestion can never express that
/// confirmation is unnecessary — downloading/launching the external client
/// always requires explicit user confirmation.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HandoffConfirmation {
    #[default]
    Required,
}

/// External-client handoff advice (MED-19, PL-015).
///
/// This is a suggestion, not a cast mode: it holds no media URL, relay
/// token, receiver session, capturer, encoder or WebRTC transport, and the
/// browser must never report "casting started" for it.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ExternalClientHandoff {
    /// Why Direct/Relay are unavailable. The serde default exists solely for
    /// the legacy `mirror` read window (see `HandoffReason::LegacyMirror`);
    /// new decisions always set a concrete reason.
    #[serde(default)]
    reason: HandoffReason,
    /// Always `Required`; defaults keep legacy `mirror` reads working.
    #[serde(default)]
    confirmation: HandoffConfirmation,
}

impl ExternalClientHandoff {
    #[must_use]
    pub const fn new(reason: HandoffReason) -> Self {
        Self {
            reason,
            confirmation: HandoffConfirmation::Required,
        }
    }

    #[must_use]
    pub const fn reason(&self) -> HandoffReason {
        self.reason
    }

    #[must_use]
    pub const fn confirmation(&self) -> HandoffConfirmation {
        self.confirmation
    }
}

/// Policy-engine decision (§9.2). `Reject` always carries a stable
/// `CoreError` code instead of a natural-language reason.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "decision", rename_all = "snake_case")]
#[serde(deny_unknown_fields)]
pub enum CastPolicyDecision {
    /// Receiver pulls the original stream URL directly (no special headers).
    Direct,
    /// Receiver pulls through the session relay, which holds the required
    /// Referer/UA upstream headers (headers never reach the receiver).
    Relay,
    /// Suggest handing off to the external Crayon cast client (MED-19).
    /// Not a cast mode: creates no receiver handle, relay token or WebRTC
    /// transport. The `mirror` alias is the v1 compatibility read window:
    /// legacy `mirror` decisions still deserialize (reason `LegacyMirror`)
    /// but are never re-emitted under the old tag.
    #[serde(alias = "mirror")]
    ExternalClientHandoff(ExternalClientHandoff),
    Reject {
        reason: CoreError,
    },
}
