//! Product-facing DTOs crossing the `CastFacade` boundary (SDK-03).
//!
//! Wire/privacy rules (CS-002, AG-007, RL-014):
//! - devices are identified by `DeviceId` only; host, IP, location, port,
//!   UDN and receiver control URLs never appear in these types;
//! - `PlaybackPosition` deliberately has no track URI field (the SDK field
//!   carries the media URL);
//! - `CastMediaUrl` carries the planned Direct/Relay URL, which may embed a
//!   signed upstream query or an opaque relay token path: it is not
//!   serializable and its `Debug` is redacted, mirroring the `SessionSecret`
//!   convention;
//! - every serializable struct uses `deny_unknown_fields`.

use crate::error::CastError;
use crayon_domain::{DeviceId, SessionGeneration, SessionId};
use serde::{de::Error as _, Deserialize, Deserializer, Serialize, Serializer};
use std::fmt::{Debug, Display, Formatter};

/// Discovery state of a receiver, mapped from the SDK discovery state.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceState {
    /// Fully resolved and ready for control.
    Ready,
    /// Discovered but device description is incomplete.
    Incomplete,
    /// Receiver demands an authorization step the product does not perform.
    RequiresAuthorization,
    /// Not re-announced recently; may still accept connections.
    Stale,
    /// Unreachable or description resolution failed.
    Offline,
}

/// Device snapshot consumed by UI and Agent reads (CS-001/CS-002).
///
/// Carries no network locator: the adapter derives `device_id` from the
/// SDK's stable device key, so a device keeps its identity across IP changes
/// and same-name/UDN conflicts, and callers can never cache an IP.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DiscoveredDevice {
    device_id: DeviceId,
    /// Receiver-reported display name (untrusted presentation data).
    friendly_name: String,
    state: DeviceState,
    /// Whether the receiver runs the Crayon receiver app (CastExtension).
    is_crayon_receiver: bool,
}

impl DiscoveredDevice {
    #[must_use]
    pub fn new(
        device_id: DeviceId,
        friendly_name: String,
        state: DeviceState,
        is_crayon_receiver: bool,
    ) -> Self {
        Self {
            device_id,
            friendly_name,
            state,
            is_crayon_receiver,
        }
    }

    #[must_use]
    pub const fn device_id(&self) -> &DeviceId {
        &self.device_id
    }

    #[must_use]
    pub fn friendly_name(&self) -> &str {
        &self.friendly_name
    }

    #[must_use]
    pub const fn state(&self) -> DeviceState {
        self.state
    }

    #[must_use]
    pub const fn is_crayon_receiver(&self) -> bool {
        self.is_crayon_receiver
    }
}

/// Length of a normalized cast code, fixed by the pinned SDK codec
/// (`cast_code_codec::CAST_CODE_LENGTH`).
const CAST_CODE_LEN: usize = 6;

/// Validated, normalized cast code (CS-003).
///
/// Normalization matches the SDK codec's input hygiene (strip whitespace,
/// `-` and U+3000, uppercase ASCII); validation checks length and an ASCII
/// alphanumeric superset of the codec alphabet. The SDK codec remains the
/// authority on the exact alphabet and checksum — codes rejected there
/// surface as `CastError::InvalidCastCode` via the cast-code call site.
#[derive(Clone, Eq, PartialEq)]
pub struct CastCode(String);

impl CastCode {
    /// Creates a normalized cast code; malformed input is rejected with
    /// `CastError::InvalidCastCode` before reaching the SDK (CS-003).
    pub fn new(raw: &str) -> Result<Self, CastError> {
        let normalized: String = raw
            .chars()
            .filter(|ch| !ch.is_whitespace() && *ch != '-' && *ch != '\u{3000}')
            .map(|ch| ch.to_ascii_uppercase())
            .collect();
        if normalized.len() != CAST_CODE_LEN
            || !normalized.bytes().all(|b| b.is_ascii_alphanumeric())
        {
            return Err(CastError::InvalidCastCode);
        }
        Ok(Self(normalized))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Display for CastCode {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl Debug for CastCode {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        Display::fmt(self, formatter)
    }
}

impl Serialize for CastCode {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(&self.0)
    }
}

impl<'de> Deserialize<'de> for CastCode {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let raw = String::deserialize(deserializer)?;
        Self::new(&raw).map_err(D::Error::custom)
    }
}

/// Media kinds the product assesses and delivers: progressive MP4 video or
/// HLS. Image/RTSP/file SDK variants are outside the product contract.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CastMediaKind {
    /// Progressive MP4 video (SDK `CastMediaType::Video`).
    Video,
    Hls,
}

/// Outcome of a receiver capability assessment for one media kind.
///
/// The pinned SDK assesses per media type only; it does not report the
/// codec/resolution matrix of `ReceiverCapabilities`. SDK-08 owns the
/// conservative mapping from this assessment into `ReceiverCapabilities`
/// (including TTL/cache invalidation); unknown or risky must never be
/// presented as supported there.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AssessmentStatus {
    Supported,
    /// Playable with known caveats (SDK `Risky`).
    Risky,
    Unsupported,
    /// Receiver profile unknown; fail closed in policy (PL-013).
    Unknown,
}

/// Receiver capability assessment for one media kind (CS-004).
///
/// Carries no SDK reason strings or user messages — only the stable status.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReceiverAssessment {
    device_id: DeviceId,
    media: CastMediaKind,
    status: AssessmentStatus,
}

impl ReceiverAssessment {
    #[must_use]
    pub const fn new(device_id: DeviceId, media: CastMediaKind, status: AssessmentStatus) -> Self {
        Self {
            device_id,
            media,
            status,
        }
    }

    #[must_use]
    pub const fn device_id(&self) -> &DeviceId {
        &self.device_id
    }

    #[must_use]
    pub const fn media(&self) -> CastMediaKind {
        self.media
    }

    #[must_use]
    pub const fn status(&self) -> AssessmentStatus {
        self.status
    }
}

/// Wire protocol of one delivery. Relay-delivered media keeps its upstream
/// protocol (the relay serves MP4 or HLS to the receiver).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeliveryProtocol {
    Mp4,
    Hls,
}

/// Upper bound for a media URL handed to the facade (defense-in-depth;
/// receivers and the relay enforce their own stricter limits).
const MAX_MEDIA_URL_LEN: usize = 4096;

/// Validated Direct/Relay media URL for one delivery (CS-005).
///
/// Deliberately not serializable and `Debug`-redacted (signed query / relay
/// token path must not leak into logs or wire types). The URL is forwarded
/// to the receiver by the SDK unchanged — the adapter never rewrites,
/// truncates, or reassembles it (PL-002).
#[derive(Clone, Eq, PartialEq)]
pub struct CastMediaUrl(String);

impl CastMediaUrl {
    /// Accepts only absolute `http`/`https` URLs within the length bound.
    pub fn new(url: &str) -> Result<Self, CastError> {
        if url.len() > MAX_MEDIA_URL_LEN {
            return Err(CastError::InvalidInput);
        }
        if !(url.starts_with("http://") || url.starts_with("https://")) {
            return Err(CastError::InvalidInput);
        }
        Ok(Self(url.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Debug for CastMediaUrl {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("CastMediaUrl(REDACTED)")
    }
}

/// One planned Direct/HLS/Relay-URL delivery (CS-005).
///
/// Pure data: it never constructs a receiver descriptor, and it cannot
/// express mirroring, WebRTC, or external-client handoff (MED-19). Not
/// serializable because it embeds the media URL.
#[derive(Clone, Eq, PartialEq)]
pub struct CastMediaRequest {
    /// Target device. Must equal the currently connected device; the adapter
    /// fails closed with `CastError::InvalidState` on mismatch.
    device_id: DeviceId,
    protocol: DeliveryProtocol,
    url: CastMediaUrl,
}

impl CastMediaRequest {
    #[must_use]
    pub const fn new(device_id: DeviceId, protocol: DeliveryProtocol, url: CastMediaUrl) -> Self {
        Self {
            device_id,
            protocol,
            url,
        }
    }

    #[must_use]
    pub const fn device_id(&self) -> &DeviceId {
        &self.device_id
    }

    #[must_use]
    pub const fn protocol(&self) -> DeliveryProtocol {
        self.protocol
    }

    #[must_use]
    pub const fn url(&self) -> &CastMediaUrl {
        &self.url
    }
}

impl Debug for CastMediaRequest {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("CastMediaRequest")
            .field("device_id", &self.device_id)
            .field("protocol", &self.protocol)
            .field("url", &self.url)
            .finish()
    }
}

/// Fencing reference to a live cast session: identity + generation only
/// (CS-006).
///
/// Distinct from `crayon_ipc_schema::SessionGrant`, which authorizes local
/// IPC/relay routes; this type fences receiver session control. Handles
/// carrying an older generation are rejected by the adapter, and supervision
/// events carrying one are dropped by consumers.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CastSessionRef {
    session_id: SessionId,
    generation: SessionGeneration,
}

impl CastSessionRef {
    #[must_use]
    pub const fn new(session_id: SessionId, generation: SessionGeneration) -> Self {
        Self {
            session_id,
            generation,
        }
    }

    #[must_use]
    pub const fn session_id(&self) -> &SessionId {
        &self.session_id
    }

    #[must_use]
    pub const fn generation(&self) -> SessionGeneration {
        self.generation
    }
}

/// Receiver volume, 0..=100 (matches the pinned SDK `set_volume` bound).
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct Volume(u8);

impl Volume {
    pub const MAX: u8 = 100;

    pub fn new(value: u8) -> Result<Self, CastError> {
        if value > Self::MAX {
            return Err(CastError::InvalidInput);
        }
        Ok(Self(value))
    }

    #[must_use]
    pub const fn get(self) -> u8 {
        self.0
    }
}

impl Serialize for Volume {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_u8(self.0)
    }
}

impl<'de> Deserialize<'de> for Volume {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let raw = u8::deserialize(deserializer)?;
        Self::new(raw).map_err(D::Error::custom)
    }
}

/// Playback position report (CS-006).
///
/// The SDK's `track_uri` field is dropped at the boundary: it is the media
/// URL and must not cross into product DTOs (RL-014).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PlaybackPosition {
    position_seconds: Option<u64>,
    duration_seconds: Option<u64>,
}

impl PlaybackPosition {
    #[must_use]
    pub const fn new(position_seconds: Option<u64>, duration_seconds: Option<u64>) -> Self {
        Self {
            position_seconds,
            duration_seconds,
        }
    }

    #[must_use]
    pub const fn position_seconds(self) -> Option<u64> {
        self.position_seconds
    }

    #[must_use]
    pub const fn duration_seconds(self) -> Option<u64> {
        self.duration_seconds
    }
}

/// Lifecycle phase of a supervised cast session.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CastSessionPhase {
    Starting,
    Active,
    Suspended,
    Recovering,
    Terminating,
    Terminated,
}

/// Playback state of a supervised cast session.
///
/// The SDK's static-image state (`presenting_static`) has no product
/// counterpart: the facade never delivers images.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CastPlaybackState {
    Unknown,
    Preparing,
    Buffering,
    Playing,
    Paused,
    Ended,
    Stopped,
    Failed,
}

/// Why a cast session terminated (CS-007). Mirrors the pinned SDK terminal
/// reasons 1:1 so UI/runtime can converge resources per cause: natural end,
/// receiver-side stop, route loss, replacement by a new cast or another
/// controller, and source/protocol failures.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CastTerminalReason {
    StoppedBySender,
    StoppedByReceiver,
    EndedNormally,
    ReplacedByNewCast,
    ReplacedByOtherController,
    ReceiverShutdown,
    ReceiverSessionLost,
    ReceiverUnreachable,
    PlaybackFailed,
    SourceFailed,
    ProtocolError,
}

/// Session supervision snapshot (CS-007).
///
/// Carries all fencing data a consumer needs: the session reference
/// (identity + generation) and a monotonic `state_revision` within the
/// generation. Consumers must drop any event for which `supersedes` against
/// the last applied snapshot is false — an older-generation event must never
/// stop or mutate a newer session.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CastSessionSnapshot {
    session: CastSessionRef,
    phase: CastSessionPhase,
    playback: CastPlaybackState,
    state_revision: u64,
    terminal_reason: Option<CastTerminalReason>,
}

impl CastSessionSnapshot {
    #[must_use]
    pub const fn new(
        session: CastSessionRef,
        phase: CastSessionPhase,
        playback: CastPlaybackState,
        state_revision: u64,
        terminal_reason: Option<CastTerminalReason>,
    ) -> Self {
        Self {
            session,
            phase,
            playback,
            state_revision,
            terminal_reason,
        }
    }

    #[must_use]
    pub const fn session(&self) -> &CastSessionRef {
        &self.session
    }

    #[must_use]
    pub const fn phase(&self) -> CastSessionPhase {
        self.phase
    }

    #[must_use]
    pub const fn playback(&self) -> CastPlaybackState {
        self.playback
    }

    #[must_use]
    pub const fn state_revision(&self) -> u64 {
        self.state_revision
    }

    #[must_use]
    pub const fn terminal_reason(&self) -> Option<CastTerminalReason> {
        self.terminal_reason
    }

    #[must_use]
    pub const fn is_terminal(&self) -> bool {
        matches!(self.phase, CastSessionPhase::Terminated)
    }

    /// Fencing rule (CS-006/CS-007), mirroring the SDK event hub: a newer
    /// generation always wins; within the same session and generation only a
    /// higher `state_revision` wins. Anything else is a stale event and must
    /// be dropped.
    #[must_use]
    pub fn supersedes(&self, current: &Self) -> bool {
        self.session
            .generation()
            .supersedes(current.session.generation())
            || (self.session.generation() == current.session.generation()
                && self.session.session_id() == current.session.session_id()
                && self.state_revision > current.state_revision)
    }
}
