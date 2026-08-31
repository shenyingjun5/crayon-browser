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
    }
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
