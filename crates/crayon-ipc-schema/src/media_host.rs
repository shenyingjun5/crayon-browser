//! PLT-M05b2b1: private, deterministic Browser <-> Rust media-host protocol.
//!
//! Request types intentionally do not implement `Debug`: page/media URLs may
//! contain short-lived signatures and are private in-memory transport data.

use crate::{
    AdContinuity, CastPolicyDecision, ExternalClientHandoff, HandoffReason, HeadersClass,
    ProtocolKind,
};
use crayon_domain::{CoreError, ReceiverCapabilities};
use std::error::Error;
use std::fmt::{Display, Formatter};

const MAGIC: &[u8; 4] = b"MHV1";
const VERSION: u16 = 1;
const HEADER_BYTES: usize = 8;
pub const MAX_MEDIA_HOST_FRAME_BYTES: usize = 16 * 1024;
const MAX_ID_BYTES: usize = 128;
const MAX_URL_BYTES: usize = 2_048;
const MAX_ORIGIN_BYTES: usize = 512;
pub const MAX_MEDIA_HOST_DEVICES: usize = 64;
pub const MAX_MEDIA_HOST_DEVICE_PAGE: usize = 16;
pub const MAX_MEDIA_HOST_DEVICE_NAME_BYTES: usize = 512;
pub const MAX_MEDIA_HOST_SESSION_EVENTS: usize = 64;
pub const MAX_MEDIA_HOST_CAST_CODE_BYTES: usize = 32;
pub const MAX_MEDIA_HOST_SEEK_SECONDS: u64 = 7 * 24 * 60 * 60;
const MAX_EXACT_F64_INTEGER: u64 = 9_007_199_254_740_992;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostSource {
    CurrentSrc = 0,
    NetworkRequest = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MediaHostPlayback {
    pub position_ms: u64,
    pub duration_ms: Option<u64>,
    pub is_live: bool,
    pub ad_continuity: AdContinuity,
    pub current_src: bool,
    pub near_play_event: bool,
    pub audible: bool,
    pub main_frame: bool,
    pub visible_area_px: u32,
}

#[derive(Clone, PartialEq)]
pub struct MediaHostUrlFact {
    pub request_id: String,
    pub tab_id: String,
    pub navigation_id: u64,
    pub generation: u64,
    pub observed_at_ms: u64,
    pub page_url: String,
    pub media_url: String,
    pub source: MediaHostSource,
    pub headers_class: HeadersClass,
    pub playback: Option<MediaHostPlayback>,
    pub eme_encrypted: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostDiscoveryAction {
    Start = 0,
    Stop = 1,
    Refresh = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostDeviceState {
    Ready = 0,
    Incomplete = 1,
    RequiresAuthorization = 2,
    Stale = 3,
    Offline = 4,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MediaHostDevice {
    pub device_id: String,
    pub display_name: String,
    pub state: MediaHostDeviceState,
    pub is_crayon_receiver: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostDeliveryRoute {
    Direct = 0,
    Relay = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostCastErrorCode {
    DeviceNotFound = 0,
    InvalidCastCode = 1,
    InvalidInput = 2,
    InvalidState = 3,
    NoActiveSession = 4,
    StaleSessionGeneration = 5,
    CastStartFailed = 6,
    UnsupportedByReceiver = 7,
    RouteLost = 8,
    NetworkUnavailable = 9,
    ReceiverUnreachable = 10,
    ReceiverProtocol = 11,
    Internal = 12,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MediaHostCastStartOutcome {
    Casting {
        session_generation: u64,
        route: MediaHostDeliveryRoute,
    },
    Handoff {
        reason: HandoffReason,
    },
    Rejected {
        reason: CoreError,
    },
    Failed {
        code: MediaHostCastErrorCode,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostSessionPhase {
    Starting = 0,
    Active = 1,
    Suspended = 2,
    Recovering = 3,
    Terminating = 4,
    Terminated = 5,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostSessionPlayback {
    Unknown = 0,
    Preparing = 1,
    Buffering = 2,
    Playing = 3,
    Paused = 4,
    Ended = 5,
    Stopped = 6,
    Failed = 7,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostTerminalReason {
    StoppedBySender = 0,
    StoppedByReceiver = 1,
    EndedNormally = 2,
    ReplacedByNewCast = 3,
    ReplacedByOtherController = 4,
    ReceiverShutdown = 5,
    ReceiverSessionLost = 6,
    ReceiverUnreachable = 7,
    PlaybackFailed = 8,
    SourceFailed = 9,
    ProtocolError = 10,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MediaHostResolveCastCodeOutcome {
    Resolved(MediaHostDevice),
    Failed(MediaHostCastErrorCode),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostCastControlAction {
    Play = 0,
    Pause = 1,
    Seek = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MediaHostCastControlOutcome {
    Applied,
    Failed(MediaHostCastErrorCode),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MediaHostSessionEvent {
    pub session_generation: u64,
    pub state_revision: u64,
    pub phase: MediaHostSessionPhase,
    pub playback: MediaHostSessionPlayback,
    pub terminal_reason: Option<MediaHostTerminalReason>,
}

#[derive(Clone, PartialEq)]
pub enum MediaHostMessage {
    IngestUrl(MediaHostUrlFact),
    MarkEme {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
    },
    Decide {
        request_id: String,
        candidate_id: u64,
        now_ms: u64,
        receiver: ReceiverCapabilities,
        handoff_available: bool,
    },
    DecideUrlLess {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
        page_url: String,
        playback: MediaHostPlayback,
        eme_encrypted: bool,
        handoff_available: bool,
    },
    Cancel {
        request_id: String,
    },
    Navigation {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
    },
    CloseTab {
        request_id: String,
        tab_id: String,
        generation: u64,
    },
    Shutdown,
    CandidateReply {
        request_id: String,
        candidate_id: Option<u64>,
        redacted_origin: String,
    },
    DecisionReply {
        request_id: String,
        candidate_id: Option<u64>,
        protocol: Option<ProtocolKind>,
        decision: CastPolicyDecision,
    },
    Ack {
        request_id: String,
    },
    ErrorReply {
        request_id: String,
        code: MediaHostErrorCode,
    },
    Discovery {
        request_id: String,
        action: MediaHostDiscoveryAction,
    },
    ListDevices {
        request_id: String,
        snapshot_revision: Option<u64>,
        offset: u16,
    },
    DevicePageReply {
        request_id: String,
        snapshot_revision: u64,
        offset: u16,
        next_offset: Option<u16>,
        devices: Vec<MediaHostDevice>,
    },
    StartCast {
        request_id: String,
        candidate_id: u64,
        device_id: String,
        handoff_available: bool,
    },
    StartCastReply {
        request_id: String,
        outcome: MediaHostCastStartOutcome,
    },
    StopCast {
        request_id: String,
        session_generation: u64,
    },
    PollSessionEvents {
        request_id: String,
    },
    SessionEventsReply {
        request_id: String,
        dropped_events: u64,
        events: Vec<MediaHostSessionEvent>,
    },
    ResolveCastCode {
        request_id: String,
        cast_code: String,
    },
    ResolveCastCodeReply {
        request_id: String,
        outcome: MediaHostResolveCastCodeOutcome,
    },
    ControlCast {
        request_id: String,
        session_generation: u64,
        action: MediaHostCastControlAction,
        position_seconds: Option<u64>,
    },
    ControlCastReply {
        request_id: String,
        session_generation: u64,
        outcome: MediaHostCastControlOutcome,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum MediaHostErrorCode {
    InvalidMessage = 0,
    InvalidState = 1,
    StaleContext = 2,
    CapacityExceeded = 3,
    Cancelled = 4,
    CandidateUnavailable = 5,
    HostUnavailable = 6,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MediaHostError {
    FrameTooLarge,
    InvalidMagic,
    UnsupportedVersion,
    UnknownKind,
    InvalidFlags,
    Truncated,
    TrailingBytes,
    InvalidUtf8,
    InvalidValue,
    LengthExceeded,
}

impl Display for MediaHostError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::FrameTooLarge => "media-host frame exceeds size limit",
            Self::InvalidMagic => "media-host magic rejected",
            Self::UnsupportedVersion => "media-host version rejected",
            Self::UnknownKind => "media-host message kind rejected",
            Self::InvalidFlags => "media-host flags rejected",
            Self::Truncated => "media-host frame truncated",
            Self::TrailingBytes => "media-host frame has trailing bytes",
            Self::InvalidUtf8 => "media-host string is not UTF-8",
            Self::InvalidValue => "media-host value rejected",
            Self::LengthExceeded => "media-host field exceeds size limit",
        })
    }
}

impl Error for MediaHostError {}

#[repr(u8)]
enum Kind {
    IngestUrl = 1,
    MarkEme = 2,
    Decide = 3,
    DecideUrlLess = 4,
    Cancel = 5,
    Navigation = 6,
    CloseTab = 7,
    Shutdown = 8,
    CandidateReply = 9,
    DecisionReply = 10,
    Ack = 11,
    ErrorReply = 12,
    Discovery = 13,
    ListDevices = 14,
    DevicePageReply = 15,
    StartCast = 16,
    StartCastReply = 17,
    StopCast = 18,
    PollSessionEvents = 19,
    SessionEventsReply = 20,
    ResolveCastCode = 21,
    ResolveCastCodeReply = 22,
    ControlCast = 23,
    ControlCastReply = 24,
}

pub fn encode_media_host_message(message: &MediaHostMessage) -> Result<Vec<u8>, MediaHostError> {
    let mut writer = Writer::new(kind_of(message));
    match message {
        MediaHostMessage::IngestUrl(fact) => encode_url_fact(&mut writer, fact)?,
        MediaHostMessage::MarkEme {
            request_id,
            tab_id,
            navigation_id,
            generation,
        }
        | MediaHostMessage::Navigation {
            request_id,
            tab_id,
            navigation_id,
            generation,
        } => {
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
        }
        MediaHostMessage::Decide {
            request_id,
            candidate_id,
            now_ms,
            receiver,
            handoff_available,
        } => {
            writer.id(request_id)?;
            writer.nonzero_u64(*candidate_id)?;
            writer.u64(*now_ms);
            encode_receiver(&mut writer, *receiver);
            writer.boolean(*handoff_available);
        }
        MediaHostMessage::DecideUrlLess {
            request_id,
            tab_id,
            navigation_id,
            generation,
            page_url,
            playback,
            eme_encrypted,
            handoff_available,
        } => {
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
            writer.url(page_url)?;
            encode_playback(&mut writer, playback)?;
            writer.boolean(*eme_encrypted);
            writer.boolean(*handoff_available);
        }
        MediaHostMessage::Cancel { request_id } => writer.id(request_id)?,
        MediaHostMessage::CloseTab {
            request_id,
            tab_id,
            generation,
        } => {
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*generation)?;
        }
        MediaHostMessage::Shutdown => {}
        MediaHostMessage::CandidateReply {
            request_id,
            candidate_id,
            redacted_origin,
        } => {
            writer.id(request_id)?;
            writer.optional_nonzero_u64(*candidate_id)?;
            writer.origin(redacted_origin)?;
            if candidate_id.is_none() != redacted_origin.is_empty() {
                return Err(MediaHostError::InvalidValue);
            }
        }
        MediaHostMessage::DecisionReply {
            request_id,
            candidate_id,
            protocol,
            decision,
        } => {
            writer.id(request_id)?;
            writer.optional_nonzero_u64(*candidate_id)?;
            writer.u8(protocol.map_or(u8::MAX, protocol_code));
            if candidate_id.is_some() != protocol.is_some() {
                return Err(MediaHostError::InvalidValue);
            }
            encode_decision(&mut writer, *decision);
        }
        MediaHostMessage::Ack { request_id } => writer.id(request_id)?,
        MediaHostMessage::ErrorReply { request_id, code } => {
            writer.id(request_id)?;
            writer.u8(*code as u8);
        }
        MediaHostMessage::Discovery { request_id, action } => {
            writer.id(request_id)?;
            writer.u8(*action as u8);
        }
        MediaHostMessage::ListDevices {
            request_id,
            snapshot_revision,
            offset,
        } => {
            writer.id(request_id)?;
            writer.optional_nonzero_u64(*snapshot_revision)?;
            validate_device_page_request(*snapshot_revision, *offset)?;
            writer.u16(*offset);
        }
        MediaHostMessage::DevicePageReply {
            request_id,
            snapshot_revision,
            offset,
            next_offset,
            devices,
        } => encode_device_page(
            &mut writer,
            request_id,
            *snapshot_revision,
            *offset,
            *next_offset,
            devices,
        )?,
        MediaHostMessage::StartCast {
            request_id,
            candidate_id,
            device_id,
            handoff_available,
        } => {
            writer.id(request_id)?;
            writer.nonzero_u64(*candidate_id)?;
            writer.device_id(device_id)?;
            writer.boolean(*handoff_available);
        }
        MediaHostMessage::StartCastReply {
            request_id,
            outcome,
        } => {
            writer.id(request_id)?;
            encode_cast_start_outcome(&mut writer, *outcome)?;
        }
        MediaHostMessage::StopCast {
            request_id,
            session_generation,
        } => {
            writer.id(request_id)?;
            writer.nonzero_u64(*session_generation)?;
        }
        MediaHostMessage::PollSessionEvents { request_id } => writer.id(request_id)?,
        MediaHostMessage::SessionEventsReply {
            request_id,
            dropped_events,
            events,
        } => encode_session_events(&mut writer, request_id, *dropped_events, events)?,
        MediaHostMessage::ResolveCastCode {
            request_id,
            cast_code,
        } => {
            writer.id(request_id)?;
            writer.cast_code(cast_code)?;
        }
        MediaHostMessage::ResolveCastCodeReply {
            request_id,
            outcome,
        } => {
            writer.id(request_id)?;
            encode_resolve_cast_code_outcome(&mut writer, outcome)?;
        }
        MediaHostMessage::ControlCast {
            request_id,
            session_generation,
            action,
            position_seconds,
        } => {
            writer.id(request_id)?;
            writer.nonzero_u64(*session_generation)?;
            writer.u8(*action as u8);
            validate_cast_control(*action, *position_seconds)?;
            writer.optional_u64(*position_seconds)?;
        }
        MediaHostMessage::ControlCastReply {
            request_id,
            session_generation,
            outcome,
        } => {
            writer.id(request_id)?;
            writer.nonzero_u64(*session_generation)?;
            encode_cast_control_outcome(&mut writer, *outcome);
        }
    }
    writer.finish()
}

pub fn decode_media_host_message(bytes: &[u8]) -> Result<MediaHostMessage, MediaHostError> {
    if bytes.len() > MAX_MEDIA_HOST_FRAME_BYTES {
        return Err(MediaHostError::FrameTooLarge);
    }
    if bytes.len() < HEADER_BYTES {
        return Err(MediaHostError::Truncated);
    }
    if &bytes[..4] != MAGIC {
        return Err(MediaHostError::InvalidMagic);
    }
    if u16::from_be_bytes([bytes[4], bytes[5]]) != VERSION {
        return Err(MediaHostError::UnsupportedVersion);
    }
    if bytes[7] != 0 {
        return Err(MediaHostError::InvalidFlags);
    }
    let mut reader = Reader::new(&bytes[HEADER_BYTES..]);
    let message = match bytes[6] {
        1 => MediaHostMessage::IngestUrl(decode_url_fact(&mut reader)?),
        2 => MediaHostMessage::MarkEme {
            request_id: reader.id()?,
            tab_id: reader.tab_id()?,
            navigation_id: reader.nonzero_u64()?,
            generation: reader.nonzero_u64()?,
        },
        3 => MediaHostMessage::Decide {
            request_id: reader.id()?,
            candidate_id: reader.nonzero_u64()?,
            now_ms: reader.u64()?,
            receiver: decode_receiver(&mut reader)?,
            handoff_available: reader.boolean()?,
        },
        4 => MediaHostMessage::DecideUrlLess {
            request_id: reader.id()?,
            tab_id: reader.tab_id()?,
            navigation_id: reader.nonzero_u64()?,
            generation: reader.nonzero_u64()?,
            page_url: reader.url()?,
            playback: decode_playback(&mut reader)?,
            eme_encrypted: reader.boolean()?,
            handoff_available: reader.boolean()?,
        },
        5 => MediaHostMessage::Cancel {
            request_id: reader.id()?,
        },
        6 => MediaHostMessage::Navigation {
            request_id: reader.id()?,
            tab_id: reader.tab_id()?,
            navigation_id: reader.nonzero_u64()?,
            generation: reader.nonzero_u64()?,
        },
        7 => MediaHostMessage::CloseTab {
            request_id: reader.id()?,
            tab_id: reader.tab_id()?,
            generation: reader.nonzero_u64()?,
        },
        8 => MediaHostMessage::Shutdown,
        9 => {
            let request_id = reader.id()?;
            let candidate_id = reader.optional_nonzero_u64()?;
            let redacted_origin = reader.origin()?;
            if candidate_id.is_none() != redacted_origin.is_empty() {
                return Err(MediaHostError::InvalidValue);
            }
            MediaHostMessage::CandidateReply {
                request_id,
                candidate_id,
                redacted_origin,
            }
        }
        10 => {
            let request_id = reader.id()?;
            let candidate_id = reader.optional_nonzero_u64()?;
            let protocol = match reader.u8()? {
                u8::MAX => None,
                raw => Some(decode_protocol(raw)?),
            };
            if candidate_id.is_some() != protocol.is_some() {
                return Err(MediaHostError::InvalidValue);
            }
            MediaHostMessage::DecisionReply {
                request_id,
                candidate_id,
                protocol,
                decision: decode_decision(&mut reader)?,
            }
        }
        11 => MediaHostMessage::Ack {
            request_id: reader.id()?,
        },
        12 => MediaHostMessage::ErrorReply {
            request_id: reader.id()?,
            code: decode_host_error(reader.u8()?)?,
        },
        13 => MediaHostMessage::Discovery {
            request_id: reader.id()?,
            action: decode_discovery_action(reader.u8()?)?,
        },
        14 => {
            let request_id = reader.id()?;
            let snapshot_revision = reader.optional_nonzero_u64()?;
            let offset = reader.u16()?;
            validate_device_page_request(snapshot_revision, offset)?;
            MediaHostMessage::ListDevices {
                request_id,
                snapshot_revision,
                offset,
            }
        }
        15 => decode_device_page(&mut reader)?,
        16 => MediaHostMessage::StartCast {
            request_id: reader.id()?,
            candidate_id: reader.nonzero_u64()?,
            device_id: reader.device_id()?,
            handoff_available: reader.boolean()?,
        },
        17 => MediaHostMessage::StartCastReply {
            request_id: reader.id()?,
            outcome: decode_cast_start_outcome(&mut reader)?,
        },
        18 => MediaHostMessage::StopCast {
            request_id: reader.id()?,
            session_generation: reader.nonzero_u64()?,
        },
        19 => MediaHostMessage::PollSessionEvents {
            request_id: reader.id()?,
        },
        20 => decode_session_events(&mut reader)?,
        21 => MediaHostMessage::ResolveCastCode {
            request_id: reader.id()?,
            cast_code: reader.cast_code()?,
        },
        22 => MediaHostMessage::ResolveCastCodeReply {
            request_id: reader.id()?,
            outcome: decode_resolve_cast_code_outcome(&mut reader)?,
        },
        23 => {
            let request_id = reader.id()?;
            let session_generation = reader.nonzero_u64()?;
            let action = decode_cast_control_action(reader.u8()?)?;
            let position_seconds = reader.optional_u64()?;
            validate_cast_control(action, position_seconds)?;
            MediaHostMessage::ControlCast {
                request_id,
                session_generation,
                action,
                position_seconds,
            }
        }
        24 => MediaHostMessage::ControlCastReply {
            request_id: reader.id()?,
            session_generation: reader.nonzero_u64()?,
            outcome: decode_cast_control_outcome(&mut reader)?,
        },
        _ => return Err(MediaHostError::UnknownKind),
    };
    if !reader.is_empty() {
        return Err(MediaHostError::TrailingBytes);
    }
    Ok(message)
}

fn kind_of(message: &MediaHostMessage) -> Kind {
    match message {
        MediaHostMessage::IngestUrl(_) => Kind::IngestUrl,
        MediaHostMessage::MarkEme { .. } => Kind::MarkEme,
        MediaHostMessage::Decide { .. } => Kind::Decide,
        MediaHostMessage::DecideUrlLess { .. } => Kind::DecideUrlLess,
        MediaHostMessage::Cancel { .. } => Kind::Cancel,
        MediaHostMessage::Navigation { .. } => Kind::Navigation,
        MediaHostMessage::CloseTab { .. } => Kind::CloseTab,
        MediaHostMessage::Shutdown => Kind::Shutdown,
        MediaHostMessage::CandidateReply { .. } => Kind::CandidateReply,
        MediaHostMessage::DecisionReply { .. } => Kind::DecisionReply,
        MediaHostMessage::Ack { .. } => Kind::Ack,
        MediaHostMessage::ErrorReply { .. } => Kind::ErrorReply,
        MediaHostMessage::Discovery { .. } => Kind::Discovery,
        MediaHostMessage::ListDevices { .. } => Kind::ListDevices,
        MediaHostMessage::DevicePageReply { .. } => Kind::DevicePageReply,
        MediaHostMessage::StartCast { .. } => Kind::StartCast,
        MediaHostMessage::StartCastReply { .. } => Kind::StartCastReply,
        MediaHostMessage::StopCast { .. } => Kind::StopCast,
        MediaHostMessage::PollSessionEvents { .. } => Kind::PollSessionEvents,
        MediaHostMessage::SessionEventsReply { .. } => Kind::SessionEventsReply,
        MediaHostMessage::ResolveCastCode { .. } => Kind::ResolveCastCode,
        MediaHostMessage::ResolveCastCodeReply { .. } => Kind::ResolveCastCodeReply,
        MediaHostMessage::ControlCast { .. } => Kind::ControlCast,
        MediaHostMessage::ControlCastReply { .. } => Kind::ControlCastReply,
    }
}

fn validate_cast_control(
    action: MediaHostCastControlAction,
    position_seconds: Option<u64>,
) -> Result<(), MediaHostError> {
    match (action, position_seconds) {
        (MediaHostCastControlAction::Play | MediaHostCastControlAction::Pause, None) => Ok(()),
        (MediaHostCastControlAction::Seek, Some(position))
            if position <= MAX_MEDIA_HOST_SEEK_SECONDS =>
        {
            Ok(())
        }
        _ => Err(MediaHostError::InvalidValue),
    }
}

fn encode_cast_control_outcome(writer: &mut Writer, outcome: MediaHostCastControlOutcome) {
    match outcome {
        MediaHostCastControlOutcome::Applied => writer.u8(0),
        MediaHostCastControlOutcome::Failed(error) => {
            writer.u8(1);
            writer.u8(error as u8);
        }
    }
}

fn decode_cast_control_outcome(
    reader: &mut Reader<'_>,
) -> Result<MediaHostCastControlOutcome, MediaHostError> {
    match reader.u8()? {
        0 => Ok(MediaHostCastControlOutcome::Applied),
        1 => Ok(MediaHostCastControlOutcome::Failed(decode_cast_error(
            reader.u8()?,
        )?)),
        _ => Err(MediaHostError::InvalidValue),
    }
}

fn encode_resolve_cast_code_outcome(
    writer: &mut Writer,
    outcome: &MediaHostResolveCastCodeOutcome,
) -> Result<(), MediaHostError> {
    match outcome {
        MediaHostResolveCastCodeOutcome::Resolved(device) => {
            writer.u8(0);
            encode_device(writer, device)?;
        }
        MediaHostResolveCastCodeOutcome::Failed(error) => {
            writer.u8(1);
            writer.u8(*error as u8);
        }
    }
    Ok(())
}

fn decode_resolve_cast_code_outcome(
    reader: &mut Reader<'_>,
) -> Result<MediaHostResolveCastCodeOutcome, MediaHostError> {
    match reader.u8()? {
        0 => Ok(MediaHostResolveCastCodeOutcome::Resolved(decode_device(
            reader,
        )?)),
        1 => Ok(MediaHostResolveCastCodeOutcome::Failed(decode_cast_error(
            reader.u8()?,
        )?)),
        _ => Err(MediaHostError::InvalidValue),
    }
}

fn validate_device_page_request(
    snapshot_revision: Option<u64>,
    offset: u16,
) -> Result<(), MediaHostError> {
    if offset as usize >= MAX_MEDIA_HOST_DEVICES || (snapshot_revision.is_none() && offset != 0) {
        return Err(MediaHostError::InvalidValue);
    }
    Ok(())
}

fn encode_device_page(
    writer: &mut Writer,
    request_id: &str,
    snapshot_revision: u64,
    offset: u16,
    next_offset: Option<u16>,
    devices: &[MediaHostDevice],
) -> Result<(), MediaHostError> {
    validate_device_page(snapshot_revision, offset, next_offset, devices.len())?;
    validate_unique_devices(devices)?;
    writer.id(request_id)?;
    writer.nonzero_u64(snapshot_revision)?;
    writer.u16(offset);
    writer.u16(next_offset.unwrap_or(u16::MAX));
    writer.u16(devices.len() as u16);
    for device in devices {
        encode_device(writer, device)?;
    }
    Ok(())
}

fn decode_device_page(reader: &mut Reader<'_>) -> Result<MediaHostMessage, MediaHostError> {
    let request_id = reader.id()?;
    let snapshot_revision = reader.nonzero_u64()?;
    let offset = reader.u16()?;
    let next_offset = match reader.u16()? {
        u16::MAX => None,
        value => Some(value),
    };
    let count = reader.u16()? as usize;
    validate_device_page(snapshot_revision, offset, next_offset, count)?;
    let mut devices = Vec::with_capacity(count);
    for _ in 0..count {
        devices.push(decode_device(reader)?);
    }
    validate_unique_devices(&devices)?;
    Ok(MediaHostMessage::DevicePageReply {
        request_id,
        snapshot_revision,
        offset,
        next_offset,
        devices,
    })
}

fn validate_device_page(
    snapshot_revision: u64,
    offset: u16,
    next_offset: Option<u16>,
    count: usize,
) -> Result<(), MediaHostError> {
    let end = offset as usize + count;
    if snapshot_revision == 0
        || offset as usize >= MAX_MEDIA_HOST_DEVICES
        || count > MAX_MEDIA_HOST_DEVICE_PAGE
        || end > MAX_MEDIA_HOST_DEVICES
        || (count == 0 && next_offset.is_some())
        || next_offset.is_some_and(|next| next as usize != end || end >= MAX_MEDIA_HOST_DEVICES)
    {
        return Err(MediaHostError::InvalidValue);
    }
    Ok(())
}

fn validate_unique_devices(devices: &[MediaHostDevice]) -> Result<(), MediaHostError> {
    for (index, device) in devices.iter().enumerate() {
        if devices[..index]
            .iter()
            .any(|prior| prior.device_id == device.device_id)
        {
            return Err(MediaHostError::InvalidValue);
        }
    }
    Ok(())
}

fn encode_cast_start_outcome(
    writer: &mut Writer,
    outcome: MediaHostCastStartOutcome,
) -> Result<(), MediaHostError> {
    match outcome {
        MediaHostCastStartOutcome::Casting {
            session_generation,
            route,
        } => {
            writer.u8(0);
            writer.nonzero_u64(session_generation)?;
            writer.u8(route as u8);
        }
        MediaHostCastStartOutcome::Handoff { reason } => {
            writer.u8(1);
            writer.u8(handoff_reason_code(reason));
        }
        MediaHostCastStartOutcome::Rejected { reason } => {
            writer.u8(2);
            writer.u8(core_error_code(reason));
        }
        MediaHostCastStartOutcome::Failed { code } => {
            writer.u8(3);
            writer.u8(code as u8);
        }
    }
    Ok(())
}

fn decode_cast_start_outcome(
    reader: &mut Reader<'_>,
) -> Result<MediaHostCastStartOutcome, MediaHostError> {
    match reader.u8()? {
        0 => Ok(MediaHostCastStartOutcome::Casting {
            session_generation: reader.nonzero_u64()?,
            route: decode_delivery_route(reader.u8()?)?,
        }),
        1 => Ok(MediaHostCastStartOutcome::Handoff {
            reason: decode_handoff_reason(reader.u8()?)?,
        }),
        2 => Ok(MediaHostCastStartOutcome::Rejected {
            reason: decode_core_error(reader.u8()?)?,
        }),
        3 => Ok(MediaHostCastStartOutcome::Failed {
            code: decode_cast_error(reader.u8()?)?,
        }),
        _ => Err(MediaHostError::InvalidValue),
    }
}

fn encode_session_events(
    writer: &mut Writer,
    request_id: &str,
    dropped_events: u64,
    events: &[MediaHostSessionEvent],
) -> Result<(), MediaHostError> {
    if events.len() > MAX_MEDIA_HOST_SESSION_EVENTS {
        return Err(MediaHostError::InvalidValue);
    }
    writer.id(request_id)?;
    writer.u64(dropped_events);
    writer.u16(events.len() as u16);
    for event in events {
        validate_session_event(event)?;
        writer.nonzero_u64(event.session_generation)?;
        writer.nonzero_u64(event.state_revision)?;
        writer.u8(event.phase as u8);
        writer.u8(event.playback as u8);
        writer.u8(event.terminal_reason.map_or(u8::MAX, |reason| reason as u8));
    }
    Ok(())
}

fn decode_session_events(reader: &mut Reader<'_>) -> Result<MediaHostMessage, MediaHostError> {
    let request_id = reader.id()?;
    let dropped_events = reader.u64()?;
    let count = reader.u16()? as usize;
    if count > MAX_MEDIA_HOST_SESSION_EVENTS {
        return Err(MediaHostError::InvalidValue);
    }
    let mut events = Vec::with_capacity(count);
    for _ in 0..count {
        let event = MediaHostSessionEvent {
            session_generation: reader.nonzero_u64()?,
            state_revision: reader.nonzero_u64()?,
            phase: decode_session_phase(reader.u8()?)?,
            playback: decode_session_playback(reader.u8()?)?,
            terminal_reason: match reader.u8()? {
                u8::MAX => None,
                value => Some(decode_terminal_reason(value)?),
            },
        };
        validate_session_event(&event)?;
        events.push(event);
    }
    Ok(MediaHostMessage::SessionEventsReply {
        request_id,
        dropped_events,
        events,
    })
}

fn validate_session_event(event: &MediaHostSessionEvent) -> Result<(), MediaHostError> {
    let terminated = event.phase == MediaHostSessionPhase::Terminated;
    if event.session_generation == 0
        || event.state_revision == 0
        || terminated != event.terminal_reason.is_some()
    {
        return Err(MediaHostError::InvalidValue);
    }
    Ok(())
}

fn encode_url_fact(writer: &mut Writer, fact: &MediaHostUrlFact) -> Result<(), MediaHostError> {
    writer.id(&fact.request_id)?;
    writer.tab_id(&fact.tab_id)?;
    writer.nonzero_u64(fact.navigation_id)?;
    writer.nonzero_u64(fact.generation)?;
    writer.u64(fact.observed_at_ms);
    writer.url(&fact.page_url)?;
    writer.url(&fact.media_url)?;
    writer.u8(fact.source as u8);
    writer.u8(headers_code(fact.headers_class));
    writer.boolean(fact.playback.is_some());
    if let Some(playback) = &fact.playback {
        encode_playback(writer, playback)?;
    }
    writer.boolean(fact.eme_encrypted);
    Ok(())
}

fn decode_url_fact(reader: &mut Reader<'_>) -> Result<MediaHostUrlFact, MediaHostError> {
    Ok(MediaHostUrlFact {
        request_id: reader.id()?,
        tab_id: reader.tab_id()?,
        navigation_id: reader.nonzero_u64()?,
        generation: reader.nonzero_u64()?,
        observed_at_ms: reader.u64()?,
        page_url: reader.url()?,
        media_url: reader.url()?,
        source: decode_source(reader.u8()?)?,
        headers_class: decode_headers(reader.u8()?)?,
        playback: if reader.boolean()? {
            Some(decode_playback(reader)?)
        } else {
            None
        },
        eme_encrypted: reader.boolean()?,
    })
}

fn encode_playback(writer: &mut Writer, value: &MediaHostPlayback) -> Result<(), MediaHostError> {
    if value.position_ms > MAX_EXACT_F64_INTEGER
        || value
            .duration_ms
            .is_some_and(|duration| duration > MAX_EXACT_F64_INTEGER)
    {
        return Err(MediaHostError::InvalidValue);
    }
    writer.u64(value.position_ms);
    writer.optional_u64(value.duration_ms)?;
    writer.boolean(value.is_live);
    writer.u8(ad_continuity_code(value.ad_continuity));
    writer.boolean(value.current_src);
    writer.boolean(value.near_play_event);
    writer.boolean(value.audible);
    writer.boolean(value.main_frame);
    writer.u32(value.visible_area_px);
    Ok(())
}

fn decode_playback(reader: &mut Reader<'_>) -> Result<MediaHostPlayback, MediaHostError> {
    let playback = MediaHostPlayback {
        position_ms: reader.u64()?,
        duration_ms: reader.optional_u64()?,
        is_live: reader.boolean()?,
        ad_continuity: decode_ad_continuity(reader.u8()?)?,
        current_src: reader.boolean()?,
        near_play_event: reader.boolean()?,
        audible: reader.boolean()?,
        main_frame: reader.boolean()?,
        visible_area_px: reader.u32()?,
    };
    if playback.position_ms > MAX_EXACT_F64_INTEGER
        || playback
            .duration_ms
            .is_some_and(|duration| duration > MAX_EXACT_F64_INTEGER)
    {
        return Err(MediaHostError::InvalidValue);
    }
    Ok(playback)
}

fn encode_receiver(writer: &mut Writer, value: ReceiverCapabilities) {
    writer.boolean(value.mp4());
    writer.boolean(value.hls());
    writer.boolean(value.dash());
    writer.boolean(value.h264());
    writer.boolean(value.hevc());
    writer.boolean(value.av1());
    writer.u16(value.max_height());
}

fn decode_receiver(reader: &mut Reader<'_>) -> Result<ReceiverCapabilities, MediaHostError> {
    Ok(ReceiverCapabilities::new(
        reader.boolean()?,
        reader.boolean()?,
        reader.boolean()?,
        reader.boolean()?,
        reader.boolean()?,
        reader.boolean()?,
        reader.u16()?,
    ))
}

fn encode_decision(writer: &mut Writer, decision: CastPolicyDecision) {
    match decision {
        CastPolicyDecision::Direct => writer.u8(0),
        CastPolicyDecision::Relay => writer.u8(1),
        CastPolicyDecision::ExternalClientHandoff(handoff) => {
            writer.u8(2);
            writer.u8(handoff_reason_code(handoff.reason()));
        }
        CastPolicyDecision::Reject { reason } => {
            writer.u8(3);
            writer.u8(core_error_code(reason));
        }
    }
}

fn decode_decision(reader: &mut Reader<'_>) -> Result<CastPolicyDecision, MediaHostError> {
    match reader.u8()? {
        0 => Ok(CastPolicyDecision::Direct),
        1 => Ok(CastPolicyDecision::Relay),
        2 => Ok(CastPolicyDecision::ExternalClientHandoff(
            ExternalClientHandoff::new(decode_handoff_reason(reader.u8()?)?),
        )),
        3 => Ok(CastPolicyDecision::Reject {
            reason: decode_core_error(reader.u8()?)?,
        }),
        _ => Err(MediaHostError::InvalidValue),
    }
}

fn headers_code(value: HeadersClass) -> u8 {
    match value {
        HeadersClass::None => 0,
        HeadersClass::RefererOnly => 1,
        HeadersClass::RefererAndUa => 2,
        HeadersClass::CredentialBound => 3,
    }
}

fn protocol_code(value: ProtocolKind) -> u8 {
    match value {
        ProtocolKind::Hls => 0,
        ProtocolKind::Dash => 1,
        ProtocolKind::Mp4 => 2,
    }
}

fn ad_continuity_code(value: AdContinuity) -> u8 {
    match value {
        AdContinuity::Preserved => 0,
        AdContinuity::NotApplicable => 1,
        AdContinuity::Unknown => 2,
    }
}

fn handoff_reason_code(value: HandoffReason) -> u8 {
    match value {
        HandoffReason::KeyRequired => 0,
        HandoffReason::NoDirectUrl => 1,
        HandoffReason::ProbeInconclusive => 2,
        HandoffReason::CredentialBound => 3,
        HandoffReason::ReceiverIncompatible => 4,
        HandoffReason::AdContinuityUnknown => 5,
        HandoffReason::StartFailed => 6,
        HandoffReason::DashRelayUnsupported => 7,
        HandoffReason::LegacyMirror => 8,
    }
}

fn core_error_code(value: CoreError) -> u8 {
    CoreError::ALL
        .iter()
        .position(|candidate| *candidate == value)
        .expect("CoreError::ALL must include every variant") as u8
}

macro_rules! closed {
    ($name:ident, $ty:ty, {$($raw:literal => $value:expr),+ $(,)?}) => {
        fn $name(raw: u8) -> Result<$ty, MediaHostError> {
            match raw { $($raw => Ok($value),)+ _ => Err(MediaHostError::InvalidValue) }
        }
    };
}

closed!(decode_source, MediaHostSource, {0=>MediaHostSource::CurrentSrc,1=>MediaHostSource::NetworkRequest});
closed!(decode_headers, HeadersClass, {0=>HeadersClass::None,1=>HeadersClass::RefererOnly,2=>HeadersClass::RefererAndUa,3=>HeadersClass::CredentialBound});
closed!(decode_protocol, ProtocolKind, {0=>ProtocolKind::Hls,1=>ProtocolKind::Dash,2=>ProtocolKind::Mp4});
closed!(decode_ad_continuity, AdContinuity, {0=>AdContinuity::Preserved,1=>AdContinuity::NotApplicable,2=>AdContinuity::Unknown});
closed!(decode_handoff_reason, HandoffReason, {0=>HandoffReason::KeyRequired,1=>HandoffReason::NoDirectUrl,2=>HandoffReason::ProbeInconclusive,3=>HandoffReason::CredentialBound,4=>HandoffReason::ReceiverIncompatible,5=>HandoffReason::AdContinuityUnknown,6=>HandoffReason::StartFailed,7=>HandoffReason::DashRelayUnsupported,8=>HandoffReason::LegacyMirror});
closed!(decode_host_error, MediaHostErrorCode, {0=>MediaHostErrorCode::InvalidMessage,1=>MediaHostErrorCode::InvalidState,2=>MediaHostErrorCode::StaleContext,3=>MediaHostErrorCode::CapacityExceeded,4=>MediaHostErrorCode::Cancelled,5=>MediaHostErrorCode::CandidateUnavailable,6=>MediaHostErrorCode::HostUnavailable});
closed!(decode_discovery_action, MediaHostDiscoveryAction, {0=>MediaHostDiscoveryAction::Start,1=>MediaHostDiscoveryAction::Stop,2=>MediaHostDiscoveryAction::Refresh});
closed!(decode_cast_control_action, MediaHostCastControlAction, {0=>MediaHostCastControlAction::Play,1=>MediaHostCastControlAction::Pause,2=>MediaHostCastControlAction::Seek});
closed!(decode_device_state, MediaHostDeviceState, {0=>MediaHostDeviceState::Ready,1=>MediaHostDeviceState::Incomplete,2=>MediaHostDeviceState::RequiresAuthorization,3=>MediaHostDeviceState::Stale,4=>MediaHostDeviceState::Offline});
closed!(decode_delivery_route, MediaHostDeliveryRoute, {0=>MediaHostDeliveryRoute::Direct,1=>MediaHostDeliveryRoute::Relay});
closed!(decode_cast_error, MediaHostCastErrorCode, {0=>MediaHostCastErrorCode::DeviceNotFound,1=>MediaHostCastErrorCode::InvalidCastCode,2=>MediaHostCastErrorCode::InvalidInput,3=>MediaHostCastErrorCode::InvalidState,4=>MediaHostCastErrorCode::NoActiveSession,5=>MediaHostCastErrorCode::StaleSessionGeneration,6=>MediaHostCastErrorCode::CastStartFailed,7=>MediaHostCastErrorCode::UnsupportedByReceiver,8=>MediaHostCastErrorCode::RouteLost,9=>MediaHostCastErrorCode::NetworkUnavailable,10=>MediaHostCastErrorCode::ReceiverUnreachable,11=>MediaHostCastErrorCode::ReceiverProtocol,12=>MediaHostCastErrorCode::Internal});
closed!(decode_session_phase, MediaHostSessionPhase, {0=>MediaHostSessionPhase::Starting,1=>MediaHostSessionPhase::Active,2=>MediaHostSessionPhase::Suspended,3=>MediaHostSessionPhase::Recovering,4=>MediaHostSessionPhase::Terminating,5=>MediaHostSessionPhase::Terminated});
closed!(decode_session_playback, MediaHostSessionPlayback, {0=>MediaHostSessionPlayback::Unknown,1=>MediaHostSessionPlayback::Preparing,2=>MediaHostSessionPlayback::Buffering,3=>MediaHostSessionPlayback::Playing,4=>MediaHostSessionPlayback::Paused,5=>MediaHostSessionPlayback::Ended,6=>MediaHostSessionPlayback::Stopped,7=>MediaHostSessionPlayback::Failed});
closed!(decode_terminal_reason, MediaHostTerminalReason, {0=>MediaHostTerminalReason::StoppedBySender,1=>MediaHostTerminalReason::StoppedByReceiver,2=>MediaHostTerminalReason::EndedNormally,3=>MediaHostTerminalReason::ReplacedByNewCast,4=>MediaHostTerminalReason::ReplacedByOtherController,5=>MediaHostTerminalReason::ReceiverShutdown,6=>MediaHostTerminalReason::ReceiverSessionLost,7=>MediaHostTerminalReason::ReceiverUnreachable,8=>MediaHostTerminalReason::PlaybackFailed,9=>MediaHostTerminalReason::SourceFailed,10=>MediaHostTerminalReason::ProtocolError});

fn decode_core_error(raw: u8) -> Result<CoreError, MediaHostError> {
    CoreError::ALL
        .get(raw as usize)
        .copied()
        .ok_or(MediaHostError::InvalidValue)
}

struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    fn new(kind: Kind) -> Self {
        let mut bytes = Vec::with_capacity(256);
        bytes.extend_from_slice(MAGIC);
        bytes.extend_from_slice(&VERSION.to_be_bytes());
        bytes.push(kind as u8);
        bytes.push(0);
        Self { bytes }
    }
    fn finish(self) -> Result<Vec<u8>, MediaHostError> {
        if self.bytes.len() > MAX_MEDIA_HOST_FRAME_BYTES {
            Err(MediaHostError::FrameTooLarge)
        } else {
            Ok(self.bytes)
        }
    }
    fn u8(&mut self, value: u8) {
        self.bytes.push(value);
    }
    fn u16(&mut self, value: u16) {
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }
    fn u32(&mut self, value: u32) {
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }
    fn u64(&mut self, value: u64) {
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }
    fn nonzero_u64(&mut self, value: u64) -> Result<(), MediaHostError> {
        if value == 0 {
            return Err(MediaHostError::InvalidValue);
        }
        self.u64(value);
        Ok(())
    }
    fn optional_nonzero_u64(&mut self, value: Option<u64>) -> Result<(), MediaHostError> {
        match value {
            Some(value) => self.nonzero_u64(value),
            None => {
                self.u64(0);
                Ok(())
            }
        }
    }
    fn optional_u64(&mut self, value: Option<u64>) -> Result<(), MediaHostError> {
        self.boolean(value.is_some());
        if let Some(value) = value {
            self.u64(value);
        }
        Ok(())
    }
    fn boolean(&mut self, value: bool) {
        self.u8(u8::from(value));
    }
    fn id(&mut self, value: &str) -> Result<(), MediaHostError> {
        if !valid_id(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.string(value, MAX_ID_BYTES, false)
    }
    fn tab_id(&mut self, value: &str) -> Result<(), MediaHostError> {
        if !valid_tab_id(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.string(value, MAX_ID_BYTES, false)
    }
    fn device_id(&mut self, value: &str) -> Result<(), MediaHostError> {
        if !valid_tab_id(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.string(value, MAX_ID_BYTES, false)
    }
    fn cast_code(&mut self, value: &str) -> Result<(), MediaHostError> {
        if !valid_cast_code_input(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.string(value, MAX_MEDIA_HOST_CAST_CODE_BYTES, false)
    }
    fn url(&mut self, value: &str) -> Result<(), MediaHostError> {
        if !valid_url(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.string(value, MAX_URL_BYTES, false)
    }
    fn origin(&mut self, value: &str) -> Result<(), MediaHostError> {
        if !value.is_empty() && !valid_origin(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.string(value, MAX_ORIGIN_BYTES, true)
    }
    fn string(&mut self, value: &str, max: usize, allow_empty: bool) -> Result<(), MediaHostError> {
        if value.len() > max {
            return Err(MediaHostError::LengthExceeded);
        }
        if (!allow_empty && value.is_empty()) || !valid_text(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.u32(value.len() as u32);
        self.bytes.extend_from_slice(value.as_bytes());
        Ok(())
    }
}

struct Reader<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, offset: 0 }
    }
    fn is_empty(&self) -> bool {
        self.offset == self.bytes.len()
    }
    fn take(&mut self, count: usize) -> Result<&'a [u8], MediaHostError> {
        let end = self
            .offset
            .checked_add(count)
            .ok_or(MediaHostError::Truncated)?;
        let value = self
            .bytes
            .get(self.offset..end)
            .ok_or(MediaHostError::Truncated)?;
        self.offset = end;
        Ok(value)
    }
    fn u8(&mut self) -> Result<u8, MediaHostError> {
        Ok(self.take(1)?[0])
    }
    fn u16(&mut self) -> Result<u16, MediaHostError> {
        let value = self.take(2)?;
        Ok(u16::from_be_bytes([value[0], value[1]]))
    }
    fn u32(&mut self) -> Result<u32, MediaHostError> {
        let value = self.take(4)?;
        Ok(u32::from_be_bytes(
            value.try_into().map_err(|_| MediaHostError::Truncated)?,
        ))
    }
    fn u64(&mut self) -> Result<u64, MediaHostError> {
        let value = self.take(8)?;
        Ok(u64::from_be_bytes(
            value.try_into().map_err(|_| MediaHostError::Truncated)?,
        ))
    }
    fn nonzero_u64(&mut self) -> Result<u64, MediaHostError> {
        let value = self.u64()?;
        (value != 0)
            .then_some(value)
            .ok_or(MediaHostError::InvalidValue)
    }
    fn optional_nonzero_u64(&mut self) -> Result<Option<u64>, MediaHostError> {
        Ok(match self.u64()? {
            0 => None,
            value => Some(value),
        })
    }
    fn optional_u64(&mut self) -> Result<Option<u64>, MediaHostError> {
        if self.boolean()? {
            Ok(Some(self.u64()?))
        } else {
            Ok(None)
        }
    }
    fn boolean(&mut self) -> Result<bool, MediaHostError> {
        match self.u8()? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(MediaHostError::InvalidValue),
        }
    }
    fn id(&mut self) -> Result<String, MediaHostError> {
        let value = self.string(MAX_ID_BYTES, false)?;
        if valid_id(&value) {
            Ok(value)
        } else {
            Err(MediaHostError::InvalidValue)
        }
    }
    fn tab_id(&mut self) -> Result<String, MediaHostError> {
        let value = self.string(MAX_ID_BYTES, false)?;
        if valid_tab_id(&value) {
            Ok(value)
        } else {
            Err(MediaHostError::InvalidValue)
        }
    }
    fn device_id(&mut self) -> Result<String, MediaHostError> {
        let value = self.string(MAX_ID_BYTES, false)?;
        if valid_tab_id(&value) {
            Ok(value)
        } else {
            Err(MediaHostError::InvalidValue)
        }
    }
    fn cast_code(&mut self) -> Result<String, MediaHostError> {
        let value = self.string(MAX_MEDIA_HOST_CAST_CODE_BYTES, false)?;
        if valid_cast_code_input(&value) {
            Ok(value)
        } else {
            Err(MediaHostError::InvalidValue)
        }
    }
    fn url(&mut self) -> Result<String, MediaHostError> {
        let value = self.string(MAX_URL_BYTES, false)?;
        if valid_url(&value) {
            Ok(value)
        } else {
            Err(MediaHostError::InvalidValue)
        }
    }
    fn origin(&mut self) -> Result<String, MediaHostError> {
        let value = self.string(MAX_ORIGIN_BYTES, true)?;
        if value.is_empty() || valid_origin(&value) {
            Ok(value)
        } else {
            Err(MediaHostError::InvalidValue)
        }
    }
    fn string(&mut self, max: usize, allow_empty: bool) -> Result<String, MediaHostError> {
        let length = self.u32()? as usize;
        if length > max {
            return Err(MediaHostError::LengthExceeded);
        }
        let raw = self.take(length)?;
        let value = std::str::from_utf8(raw).map_err(|_| MediaHostError::InvalidUtf8)?;
        if (!allow_empty && value.is_empty()) || !valid_text(value) {
            return Err(MediaHostError::InvalidValue);
        }
        Ok(value.to_owned())
    }
}

fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_ID_BYTES
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.' | b':'))
}

fn valid_tab_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_ID_BYTES
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
}

fn valid_text(value: &str) -> bool {
    value
        .chars()
        .all(|character| !character.is_control() && !(0x80..=0x9f).contains(&(character as u32)))
}

fn encode_device(writer: &mut Writer, device: &MediaHostDevice) -> Result<(), MediaHostError> {
    writer.device_id(&device.device_id)?;
    writer.string(
        &device.display_name,
        MAX_MEDIA_HOST_DEVICE_NAME_BYTES,
        false,
    )?;
    writer.u8(device.state as u8);
    writer.boolean(device.is_crayon_receiver);
    Ok(())
}

fn decode_device(reader: &mut Reader<'_>) -> Result<MediaHostDevice, MediaHostError> {
    Ok(MediaHostDevice {
        device_id: reader.device_id()?,
        display_name: reader.string(MAX_MEDIA_HOST_DEVICE_NAME_BYTES, false)?,
        state: decode_device_state(reader.u8()?)?,
        is_crayon_receiver: reader.boolean()?,
    })
}

fn valid_cast_code_input(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_MEDIA_HOST_CAST_CODE_BYTES
        && value.chars().all(|character| {
            character.is_ascii_alphanumeric()
                || character == '-'
                || character == ' '
                || character == '\u{3000}'
        })
}

fn valid_url(value: &str) -> bool {
    value.len() <= MAX_URL_BYTES
        && url::Url::parse(value).is_ok_and(|parsed| {
            matches!(parsed.scheme(), "http" | "https")
                && parsed.host_str().is_some_and(|host| !host.is_empty())
        })
}

fn valid_origin(value: &str) -> bool {
    value.len() <= MAX_ORIGIN_BYTES
        && url::Url::parse(value).is_ok_and(|parsed| {
            matches!(parsed.scheme(), "http" | "https")
                && parsed.host_str().is_some_and(|host| !host.is_empty())
                && parsed.path() == "/"
                && parsed.query().is_none()
                && parsed.fragment().is_none()
                && parsed.username().is_empty()
                && parsed.password().is_none()
        })
}
