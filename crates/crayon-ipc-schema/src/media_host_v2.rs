//! MHV2 handshake and bounded player wire. Decoding/matching does not
//! authenticate a peer, enable capabilities, or authorize commands. MHV1
//! remains a separate codec.
use crate::MediaHostError;

const MAGIC: &[u8; 4] = b"MHV2";
const VERSION: u16 = 2;
const HEADER_BYTES: usize = 8;
const MAX_URL_BYTES: usize = 2_048;
pub const HANDSHAKE_BYTES: usize = 34;
pub const MAX_FRAME_BYTES: u32 = 16 * 1024;
pub const MAX_PAGE_ITEMS: u16 = 16;
pub const CAP_MEDIA_READ: u32 = 1;
pub const CAP_DRAFT: u32 = 2;
pub const CAP_CONNECT: u32 = 4;
pub const CAP_STOP: u32 = 8;
pub const CAP_REASON: u32 = 16;
pub const CAP_SESSION: u32 = 32;
const KNOWN_CAPABILITIES: u32 =
    CAP_MEDIA_READ | CAP_DRAFT | CAP_CONNECT | CAP_STOP | CAP_REASON | CAP_SESSION;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Kind {
    Hello = 1,
    Welcome = 2,
}

const PLAYER_UPSERT_KIND: u8 = 3;
const PLAYER_REMOVE_KIND: u8 = 4;
const PLAYER_LIST_KIND: u8 = 5;
const PLAYER_PAGE_KIND: u8 = 6;
const DRAFT_COMMAND_KIND: u8 = 7;
const DRAFT_STATE_KIND: u8 = 8;
const DRAFT_STATE_REASON_KIND: u8 = 9;
const DRAFT_STATE_SESSION_KIND: u8 = 10;
const MAX_ID_BYTES: usize = 128;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum PlayerSourceKind {
    HttpUrl = 0,
    BlobUrl = 1,
    MediaStream = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlayerContext {
    pub session_id: u64,
    pub host_generation: u64,
    pub tab_id: u32,
    pub navigation_id: u64,
    pub tab_generation: u32,
    pub instance_id: u64,
    pub source_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayerFact {
    pub context: PlayerContext,
    pub observed_at_ms: u64,
    pub source_kind: PlayerSourceKind,
    pub position_ms: u64,
    pub duration_ms: Option<u64>,
    pub is_live: bool,
    pub has_video: bool,
    pub has_audio: bool,
    pub visible: bool,
    pub eme_encrypted: bool,
    pub visible_fraction_ppm: u32,
    pub page_url: String,
    pub media_url: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PlayerMessage {
    Upsert(PlayerFact),
    Remove(PlayerContext),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlayerPageContext {
    pub session_id: u64,
    pub host_generation: u64,
    pub request_id: u64,
    pub tab_id: u32,
    pub navigation_id: u64,
    pub tab_generation: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlayerListRequest {
    pub context: PlayerPageContext,
    pub snapshot_revision: u64,
    pub offset: u16,
    pub max_items: u16,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum PlayerPageStatus {
    Ok = 0,
    Stale = 1,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayerProjection {
    pub instance_id: u64,
    pub source_revision: u64,
    pub source_kind: PlayerSourceKind,
    pub has_video: bool,
    pub has_audio: bool,
    pub visible: bool,
    pub eme_encrypted: bool,
    pub redacted_origin: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlayerPageReply {
    pub context: PlayerPageContext,
    pub snapshot_revision: u64,
    pub status: PlayerPageStatus,
    pub offset: u16,
    pub next_offset: Option<u16>,
    pub players: Vec<PlayerProjection>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PlayerPageMessage {
    List(PlayerListRequest),
    Page(PlayerPageReply),
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DraftContext {
    pub session_id: u64,
    pub host_generation: u64,
    pub request_id: u64,
    pub profile_id: String,
    pub tab_id: u32,
    pub navigation_id: u64,
    pub tab_generation: u32,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum DraftAction {
    Open = 0,
    SelectMedia = 1,
    SelectDevice = 2,
    Connect = 3,
    Prepare = 4,
    ConfirmReplacement = 5,
    Commit = 6,
    Cancel = 7,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DraftMediaRef {
    pub instance_id: u64,
    pub source_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DraftCommand {
    pub context: DraftContext,
    pub action: DraftAction,
    pub draft_id: u64,
    pub draft_revision: u64,
    pub media: Option<DraftMediaRef>,
    pub device_id: String,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum DraftPhase {
    Choosing = 0,
    Connecting = 1,
    Preparing = 2,
    Prepared = 3,
    Committing = 4,
    Failed = 5,
    Expired = 6,
    Cancelled = 7,
    Committed = 8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum DraftError {
    None = 0,
    Invalid = 1,
    Stale = 2,
    Unavailable = 3,
    Denied = 4,
    Expired = 5,
    Busy = 6,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum DraftRoute {
    None = 0,
    Direct = 1,
    Relay = 2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum DraftReason {
    None = 0,
    Credentials = 1,
    Protection = 2,
    Recognized = 3,
    Unrecognized = 4,
    RedirectRefused = 5,
    UpstreamRejected = 6,
    AddressRejected = 7,
    Dns = 8,
    Connect = 9,
    Timeout = 10,
    Transport = 11,
    InvalidTarget = 12,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DraftStateReply {
    pub context: DraftContext,
    pub draft_id: u64,
    pub draft_revision: u64,
    pub phase: DraftPhase,
    pub error: DraftError,
    pub media: Option<DraftMediaRef>,
    pub device_id: String,
    pub device_connected: bool,
    pub replacement_confirmation_required: bool,
    pub route: DraftRoute,
    pub prepared_until_ms: Option<u64>,
    pub reason: DraftReason,
    pub session_generation: Option<u64>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum DraftMessage {
    Command(DraftCommand),
    State(DraftStateReply),
}

/// Capability bits describe only the caller-supplied supported/selected set.
/// Production adapters must supply actually implemented capabilities (default 0).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Handshake {
    pub kind: Kind,
    pub session_id: u64,
    pub generation: u64,
    pub capabilities: u32,
    pub max_frame_bytes: u32,
    pub max_page_items: u16,
}

impl Handshake {
    fn valid(self) -> bool {
        self.session_id != 0
            && self.generation != 0
            && self.capabilities & !KNOWN_CAPABILITIES == 0
            && (HANDSHAKE_BYTES as u32..=MAX_FRAME_BYTES).contains(&self.max_frame_bytes)
            && (1..=MAX_PAGE_ITEMS).contains(&self.max_page_items)
    }
}

/// Validates the echo and subset relation, not connection state or permission.
#[must_use]
pub fn matches_hello(hello: Handshake, welcome: Handshake) -> bool {
    hello.valid()
        && welcome.valid()
        && hello.kind == Kind::Hello
        && welcome.kind == Kind::Welcome
        && hello.session_id == welcome.session_id
        && hello.generation == welcome.generation
        && welcome.capabilities & !hello.capabilities == 0
        && welcome.max_frame_bytes <= hello.max_frame_bytes
        && welcome.max_page_items <= hello.max_page_items
}

pub fn encode(message: Handshake) -> Result<Vec<u8>, MediaHostError> {
    if !message.valid() {
        return Err(MediaHostError::InvalidValue);
    }
    let mut bytes = Vec::with_capacity(HANDSHAKE_BYTES);
    bytes.extend_from_slice(MAGIC);
    bytes.extend_from_slice(&VERSION.to_be_bytes());
    bytes.extend_from_slice(&[message.kind as u8, 0]);
    bytes.extend_from_slice(&message.session_id.to_be_bytes());
    bytes.extend_from_slice(&message.generation.to_be_bytes());
    bytes.extend_from_slice(&message.capabilities.to_be_bytes());
    bytes.extend_from_slice(&message.max_frame_bytes.to_be_bytes());
    bytes.extend_from_slice(&message.max_page_items.to_be_bytes());
    Ok(bytes)
}

pub fn decode(bytes: &[u8]) -> Result<Handshake, MediaHostError> {
    if bytes.len() > MAX_FRAME_BYTES as usize {
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
    let kind = match bytes[6] {
        1 => Kind::Hello,
        2 => Kind::Welcome,
        _ => return Err(MediaHostError::UnknownKind),
    };
    if bytes.len() < HANDSHAKE_BYTES {
        return Err(MediaHostError::Truncated);
    }
    if bytes.len() > HANDSHAKE_BYTES {
        return Err(MediaHostError::TrailingBytes);
    }
    let message = Handshake {
        kind,
        session_id: u64::from_be_bytes(array(&bytes[8..16])?),
        generation: u64::from_be_bytes(array(&bytes[16..24])?),
        capabilities: u32::from_be_bytes(array(&bytes[24..28])?),
        max_frame_bytes: u32::from_be_bytes(array(&bytes[28..32])?),
        max_page_items: u16::from_be_bytes(array(&bytes[32..34])?),
    };
    if !message.valid() {
        return Err(MediaHostError::InvalidValue);
    }
    Ok(message)
}

pub fn encode_player_message(message: &PlayerMessage) -> Result<Vec<u8>, MediaHostError> {
    let kind = match message {
        PlayerMessage::Upsert(_) => PLAYER_UPSERT_KIND,
        PlayerMessage::Remove(_) => PLAYER_REMOVE_KIND,
    };
    let mut writer = Writer::new(kind);
    match message {
        PlayerMessage::Upsert(fact) => {
            writer.context(fact.context)?;
            writer.nonzero_u64(fact.observed_at_ms)?;
            writer.u8(fact.source_kind as u8);
            writer.u64(fact.position_ms);
            writer.boolean(fact.duration_ms.is_some());
            if let Some(duration_ms) = fact.duration_ms {
                writer.u64(duration_ms);
            }
            writer.boolean(fact.is_live);
            writer.boolean(fact.has_video);
            writer.boolean(fact.has_audio);
            writer.boolean(fact.visible);
            writer.boolean(fact.eme_encrypted);
            if fact.visible_fraction_ppm > 1_000_000 {
                return Err(MediaHostError::InvalidValue);
            }
            writer.u32(fact.visible_fraction_ppm);
            writer.url(&fact.page_url, false)?;
            let media_url_required = fact.source_kind == PlayerSourceKind::HttpUrl;
            writer.url(&fact.media_url, !media_url_required)?;
            if media_url_required == fact.media_url.is_empty() {
                return Err(MediaHostError::InvalidValue);
            }
        }
        PlayerMessage::Remove(context) => writer.context(*context)?,
    }
    writer.finish()
}

pub fn decode_player_message(bytes: &[u8]) -> Result<PlayerMessage, MediaHostError> {
    validate_header(bytes)?;
    let kind = bytes[6];
    if !matches!(kind, PLAYER_UPSERT_KIND | PLAYER_REMOVE_KIND) {
        return Err(MediaHostError::UnknownKind);
    }
    let mut reader = Reader::new(&bytes[HEADER_BYTES..]);
    let context = reader.context()?;
    let message = if kind == PLAYER_REMOVE_KIND {
        PlayerMessage::Remove(context)
    } else {
        let observed_at_ms = reader.nonzero_u64()?;
        let source_kind = match reader.u8()? {
            0 => PlayerSourceKind::HttpUrl,
            1 => PlayerSourceKind::BlobUrl,
            2 => PlayerSourceKind::MediaStream,
            _ => return Err(MediaHostError::InvalidValue),
        };
        let position_ms = reader.u64()?;
        let duration_ms = if reader.boolean()? {
            Some(reader.u64()?)
        } else {
            None
        };
        let is_live = reader.boolean()?;
        let has_video = reader.boolean()?;
        let has_audio = reader.boolean()?;
        let visible = reader.boolean()?;
        let eme_encrypted = reader.boolean()?;
        let visible_fraction_ppm = reader.u32()?;
        if visible_fraction_ppm > 1_000_000 {
            return Err(MediaHostError::InvalidValue);
        }
        let page_url = reader.url(false)?;
        let media_url = reader.url(true)?;
        if (source_kind == PlayerSourceKind::HttpUrl) == media_url.is_empty() {
            return Err(MediaHostError::InvalidValue);
        }
        PlayerMessage::Upsert(PlayerFact {
            context,
            observed_at_ms,
            source_kind,
            position_ms,
            duration_ms,
            is_live,
            has_video,
            has_audio,
            visible,
            eme_encrypted,
            visible_fraction_ppm,
            page_url,
            media_url,
        })
    };
    if !reader.is_empty() {
        return Err(MediaHostError::TrailingBytes);
    }
    Ok(message)
}

pub fn encode_player_page_message(message: &PlayerPageMessage) -> Result<Vec<u8>, MediaHostError> {
    let mut writer = Writer::new(match message {
        PlayerPageMessage::List(_) => PLAYER_LIST_KIND,
        PlayerPageMessage::Page(_) => PLAYER_PAGE_KIND,
    });
    match message {
        PlayerPageMessage::List(request) => {
            writer.page_context(request.context)?;
            writer.u64(request.snapshot_revision);
            writer.u16(request.offset);
            if !(1..=MAX_PAGE_ITEMS).contains(&request.max_items) {
                return Err(MediaHostError::InvalidValue);
            }
            writer.u16(request.max_items);
        }
        PlayerPageMessage::Page(reply) => {
            writer.page_context(reply.context)?;
            writer.nonzero_u64(reply.snapshot_revision)?;
            writer.u8(reply.status as u8);
            writer.u16(reply.offset);
            writer.boolean(reply.next_offset.is_some());
            if let Some(next_offset) = reply.next_offset {
                writer.u16(next_offset);
            }
            if reply.players.len() > MAX_PAGE_ITEMS as usize {
                return Err(MediaHostError::LengthExceeded);
            }
            if reply.status == PlayerPageStatus::Stale
                && (!reply.players.is_empty() || reply.next_offset.is_some())
            {
                return Err(MediaHostError::InvalidValue);
            }
            if let Some(next_offset) = reply.next_offset {
                let expected = reply
                    .offset
                    .checked_add(reply.players.len() as u16)
                    .ok_or(MediaHostError::InvalidValue)?;
                if reply.players.is_empty() || next_offset != expected {
                    return Err(MediaHostError::InvalidValue);
                }
            }
            writer.u16(reply.players.len() as u16);
            for player in &reply.players {
                writer.nonzero_u64(player.instance_id)?;
                writer.nonzero_u64(player.source_revision)?;
                writer.u8(player.source_kind as u8);
                writer.boolean(player.has_video);
                writer.boolean(player.has_audio);
                writer.boolean(player.visible);
                writer.boolean(player.eme_encrypted);
                writer.origin(&player.redacted_origin)?;
            }
        }
    }
    writer.finish()
}

pub fn decode_player_page_message(bytes: &[u8]) -> Result<PlayerPageMessage, MediaHostError> {
    validate_header(bytes)?;
    if !matches!(bytes[6], PLAYER_LIST_KIND | PLAYER_PAGE_KIND) {
        return Err(MediaHostError::UnknownKind);
    }
    let mut reader = Reader::new(&bytes[HEADER_BYTES..]);
    let context = reader.page_context()?;
    let message = if bytes[6] == PLAYER_LIST_KIND {
        let request = PlayerListRequest {
            context,
            snapshot_revision: reader.u64()?,
            offset: reader.u16()?,
            max_items: reader.u16()?,
        };
        if !(1..=MAX_PAGE_ITEMS).contains(&request.max_items) {
            return Err(MediaHostError::InvalidValue);
        }
        PlayerPageMessage::List(request)
    } else {
        let snapshot_revision = reader.nonzero_u64()?;
        let status = match reader.u8()? {
            0 => PlayerPageStatus::Ok,
            1 => PlayerPageStatus::Stale,
            _ => return Err(MediaHostError::InvalidValue),
        };
        let offset = reader.u16()?;
        let next_offset = if reader.boolean()? {
            Some(reader.u16()?)
        } else {
            None
        };
        let count = reader.u16()?;
        if count > MAX_PAGE_ITEMS {
            return Err(MediaHostError::LengthExceeded);
        }
        let mut players = Vec::with_capacity(count as usize);
        for _ in 0..count {
            let instance_id = reader.nonzero_u64()?;
            let source_revision = reader.nonzero_u64()?;
            let source_kind = match reader.u8()? {
                0 => PlayerSourceKind::HttpUrl,
                1 => PlayerSourceKind::BlobUrl,
                2 => PlayerSourceKind::MediaStream,
                _ => return Err(MediaHostError::InvalidValue),
            };
            players.push(PlayerProjection {
                instance_id,
                source_revision,
                source_kind,
                has_video: reader.boolean()?,
                has_audio: reader.boolean()?,
                visible: reader.boolean()?,
                eme_encrypted: reader.boolean()?,
                redacted_origin: reader.origin()?,
            });
        }
        let reply = PlayerPageReply {
            context,
            snapshot_revision,
            status,
            offset,
            next_offset,
            players,
        };
        if status == PlayerPageStatus::Stale
            && (!reply.players.is_empty() || reply.next_offset.is_some())
        {
            return Err(MediaHostError::InvalidValue);
        }
        if let Some(next_offset) = reply.next_offset {
            let expected = offset
                .checked_add(reply.players.len() as u16)
                .ok_or(MediaHostError::InvalidValue)?;
            if reply.players.is_empty() || next_offset != expected {
                return Err(MediaHostError::InvalidValue);
            }
        }
        PlayerPageMessage::Page(reply)
    };
    if !reader.is_empty() {
        return Err(MediaHostError::TrailingBytes);
    }
    Ok(message)
}

pub fn encode_draft_message(message: &DraftMessage) -> Result<Vec<u8>, MediaHostError> {
    let mut writer = Writer::new(match message {
        DraftMessage::Command(_) => DRAFT_COMMAND_KIND,
        DraftMessage::State(state) if state.session_generation.is_some() => {
            DRAFT_STATE_SESSION_KIND
        }
        DraftMessage::State(state) if state.reason != DraftReason::None => DRAFT_STATE_REASON_KIND,
        DraftMessage::State(_) => DRAFT_STATE_KIND,
    });
    match message {
        DraftMessage::Command(command) => {
            writer.draft_context(&command.context)?;
            writer.u8(command.action as u8);
            writer.u64(command.draft_id);
            writer.u64(command.draft_revision);
            writer.boolean(command.media.is_some());
            if let Some(media) = command.media {
                writer.nonzero_u64(media.instance_id)?;
                writer.nonzero_u64(media.source_revision)?;
            }
            writer.text(&command.device_id, true)?;
            if !valid_draft_command(command) {
                return Err(MediaHostError::InvalidValue);
            }
        }
        DraftMessage::State(state) => {
            writer.draft_context(&state.context)?;
            writer.nonzero_u64(state.draft_id)?;
            writer.nonzero_u64(state.draft_revision)?;
            writer.u8(state.phase as u8);
            writer.u8(state.error as u8);
            writer.boolean(state.media.is_some());
            if let Some(media) = state.media {
                writer.nonzero_u64(media.instance_id)?;
                writer.nonzero_u64(media.source_revision)?;
            }
            writer.text(&state.device_id, true)?;
            writer.boolean(state.device_connected);
            writer.boolean(state.replacement_confirmation_required);
            writer.u8(state.route as u8);
            writer.boolean(state.prepared_until_ms.is_some());
            if let Some(expires) = state.prepared_until_ms {
                writer.nonzero_u64(expires)?;
            }
            if state.reason != DraftReason::None {
                writer.u8(state.reason as u8);
            }
            if let Some(session_generation) = state.session_generation {
                writer.nonzero_u64(session_generation)?;
            }
            if !valid_draft_state(state) {
                return Err(MediaHostError::InvalidValue);
            }
        }
    }
    writer.finish()
}

pub fn decode_draft_message(bytes: &[u8]) -> Result<DraftMessage, MediaHostError> {
    validate_header(bytes)?;
    if !matches!(
        bytes[6],
        DRAFT_COMMAND_KIND | DRAFT_STATE_KIND | DRAFT_STATE_REASON_KIND | DRAFT_STATE_SESSION_KIND
    ) {
        return Err(MediaHostError::UnknownKind);
    }
    let mut reader = Reader::new(&bytes[HEADER_BYTES..]);
    let context = reader.draft_context()?;
    let message = if bytes[6] == DRAFT_COMMAND_KIND {
        let action = match reader.u8()? {
            0 => DraftAction::Open,
            1 => DraftAction::SelectMedia,
            2 => DraftAction::SelectDevice,
            3 => DraftAction::Connect,
            4 => DraftAction::Prepare,
            5 => DraftAction::ConfirmReplacement,
            6 => DraftAction::Commit,
            7 => DraftAction::Cancel,
            _ => return Err(MediaHostError::InvalidValue),
        };
        let draft_id = reader.u64()?;
        let draft_revision = reader.u64()?;
        let media = if reader.boolean()? {
            Some(DraftMediaRef {
                instance_id: reader.nonzero_u64()?,
                source_revision: reader.nonzero_u64()?,
            })
        } else {
            None
        };
        let command = DraftCommand {
            context,
            action,
            draft_id,
            draft_revision,
            media,
            device_id: reader.text(true)?,
        };
        if !valid_draft_command(&command) {
            return Err(MediaHostError::InvalidValue);
        }
        DraftMessage::Command(command)
    } else {
        let has_reason = bytes[6] == DRAFT_STATE_REASON_KIND;
        let has_session = bytes[6] == DRAFT_STATE_SESSION_KIND;
        let draft_id = reader.nonzero_u64()?;
        let draft_revision = reader.nonzero_u64()?;
        let phase = match reader.u8()? {
            0 => DraftPhase::Choosing,
            1 => DraftPhase::Connecting,
            2 => DraftPhase::Preparing,
            3 => DraftPhase::Prepared,
            4 => DraftPhase::Committing,
            5 => DraftPhase::Failed,
            6 => DraftPhase::Expired,
            7 => DraftPhase::Cancelled,
            8 => DraftPhase::Committed,
            _ => return Err(MediaHostError::InvalidValue),
        };
        let error = match reader.u8()? {
            0 => DraftError::None,
            1 => DraftError::Invalid,
            2 => DraftError::Stale,
            3 => DraftError::Unavailable,
            4 => DraftError::Denied,
            5 => DraftError::Expired,
            6 => DraftError::Busy,
            _ => return Err(MediaHostError::InvalidValue),
        };
        let media = if reader.boolean()? {
            Some(DraftMediaRef {
                instance_id: reader.nonzero_u64()?,
                source_revision: reader.nonzero_u64()?,
            })
        } else {
            None
        };
        let state = DraftStateReply {
            context,
            draft_id,
            draft_revision,
            phase,
            error,
            media,
            device_id: reader.text(true)?,
            device_connected: reader.boolean()?,
            replacement_confirmation_required: reader.boolean()?,
            route: match reader.u8()? {
                0 => DraftRoute::None,
                1 => DraftRoute::Direct,
                2 => DraftRoute::Relay,
                _ => return Err(MediaHostError::InvalidValue),
            },
            prepared_until_ms: if reader.boolean()? {
                Some(reader.nonzero_u64()?)
            } else {
                None
            },
            reason: if has_reason {
                match reader.u8()? {
                    1 => DraftReason::Credentials,
                    2 => DraftReason::Protection,
                    3 => DraftReason::Recognized,
                    4 => DraftReason::Unrecognized,
                    5 => DraftReason::RedirectRefused,
                    6 => DraftReason::UpstreamRejected,
                    7 => DraftReason::AddressRejected,
                    8 => DraftReason::Dns,
                    9 => DraftReason::Connect,
                    10 => DraftReason::Timeout,
                    11 => DraftReason::Transport,
                    12 => DraftReason::InvalidTarget,
                    _ => return Err(MediaHostError::InvalidValue),
                }
            } else {
                DraftReason::None
            },
            session_generation: if has_session {
                Some(reader.nonzero_u64()?)
            } else {
                None
            },
        };
        if !valid_draft_state(&state) {
            return Err(MediaHostError::InvalidValue);
        }
        DraftMessage::State(state)
    };
    if !reader.is_empty() {
        return Err(MediaHostError::TrailingBytes);
    }
    Ok(message)
}

fn valid_draft_command(value: &DraftCommand) -> bool {
    let open = value.action == DraftAction::Open;
    if open != (value.draft_id == 0 && value.draft_revision == 0) {
        return false;
    }
    if !open && (value.draft_id == 0 || value.draft_revision == 0) {
        return false;
    }
    match value.action {
        DraftAction::SelectMedia => value.media.is_some() && value.device_id.is_empty(),
        DraftAction::SelectDevice => value.media.is_none() && valid_text(&value.device_id, false),
        _ => value.media.is_none() && value.device_id.is_empty(),
    }
}

fn valid_draft_state(value: &DraftStateReply) -> bool {
    if value.reason != DraftReason::None
        && !matches!(value.phase, DraftPhase::Prepared | DraftPhase::Failed)
    {
        return false;
    }
    if value.session_generation.is_some() != (value.phase == DraftPhase::Committed) {
        return false;
    }
    match value.phase {
        DraftPhase::Failed if value.error == DraftError::None => return false,
        DraftPhase::Expired if value.error != DraftError::Expired => return false,
        DraftPhase::Failed | DraftPhase::Expired => {}
        _ if value.error != DraftError::None => return false,
        _ => {}
    }
    if !value.device_id.is_empty() && !valid_text(&value.device_id, false) {
        return false;
    }
    let ready = matches!(value.phase, DraftPhase::Prepared | DraftPhase::Committing);
    if value.device_connected && value.device_id.is_empty() {
        return false;
    }
    if (value.route != DraftRoute::None) != ready {
        return false;
    }
    if value.replacement_confirmation_required
        && (ready || value.media.is_none() || value.device_id.is_empty())
    {
        return false;
    }
    if ready
        && (value.media.is_none()
            || value.device_id.is_empty()
            || !value.device_connected
            || value.replacement_confirmation_required)
    {
        return false;
    }
    if value.phase == DraftPhase::Prepared {
        value.prepared_until_ms.is_some()
    } else {
        value.prepared_until_ms.is_none()
    }
}

fn validate_header(bytes: &[u8]) -> Result<(), MediaHostError> {
    if bytes.len() > MAX_FRAME_BYTES as usize {
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
    Ok(())
}

struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    fn new(kind: u8) -> Self {
        let mut bytes = Vec::with_capacity(128);
        bytes.extend_from_slice(MAGIC);
        bytes.extend_from_slice(&VERSION.to_be_bytes());
        bytes.extend_from_slice(&[kind, 0]);
        Self { bytes }
    }

    fn finish(self) -> Result<Vec<u8>, MediaHostError> {
        if self.bytes.len() > MAX_FRAME_BYTES as usize {
            Err(MediaHostError::FrameTooLarge)
        } else {
            Ok(self.bytes)
        }
    }

    fn u8(&mut self, value: u8) {
        self.bytes.push(value);
    }

    fn u32(&mut self, value: u32) {
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }

    fn u16(&mut self, value: u16) {
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }

    fn u64(&mut self, value: u64) {
        self.bytes.extend_from_slice(&value.to_be_bytes());
    }

    fn nonzero_u32(&mut self, value: u32) -> Result<(), MediaHostError> {
        if value == 0 {
            return Err(MediaHostError::InvalidValue);
        }
        self.u32(value);
        Ok(())
    }

    fn nonzero_u64(&mut self, value: u64) -> Result<(), MediaHostError> {
        if value == 0 {
            return Err(MediaHostError::InvalidValue);
        }
        self.u64(value);
        Ok(())
    }

    fn boolean(&mut self, value: bool) {
        self.u8(u8::from(value));
    }

    fn context(&mut self, value: PlayerContext) -> Result<(), MediaHostError> {
        self.nonzero_u64(value.session_id)?;
        self.nonzero_u64(value.host_generation)?;
        self.nonzero_u32(value.tab_id)?;
        self.nonzero_u64(value.navigation_id)?;
        self.nonzero_u32(value.tab_generation)?;
        self.nonzero_u64(value.instance_id)?;
        self.nonzero_u64(value.source_revision)
    }

    fn page_context(&mut self, value: PlayerPageContext) -> Result<(), MediaHostError> {
        self.nonzero_u64(value.session_id)?;
        self.nonzero_u64(value.host_generation)?;
        self.nonzero_u64(value.request_id)?;
        self.nonzero_u32(value.tab_id)?;
        self.nonzero_u64(value.navigation_id)?;
        self.nonzero_u32(value.tab_generation)
    }

    fn draft_context(&mut self, value: &DraftContext) -> Result<(), MediaHostError> {
        self.nonzero_u64(value.session_id)?;
        self.nonzero_u64(value.host_generation)?;
        self.nonzero_u64(value.request_id)?;
        self.text(&value.profile_id, false)?;
        self.nonzero_u32(value.tab_id)?;
        self.nonzero_u64(value.navigation_id)?;
        self.nonzero_u32(value.tab_generation)
    }

    fn text(&mut self, value: &str, allow_empty: bool) -> Result<(), MediaHostError> {
        if !valid_text(value, allow_empty) {
            return Err(MediaHostError::InvalidValue);
        }
        self.u16(value.len() as u16);
        self.bytes.extend_from_slice(value.as_bytes());
        Ok(())
    }

    fn origin(&mut self, value: &str) -> Result<(), MediaHostError> {
        if value.len() > MAX_URL_BYTES {
            return Err(MediaHostError::LengthExceeded);
        }
        if !valid_origin(value) {
            return Err(MediaHostError::InvalidValue);
        }
        self.u32(value.len() as u32);
        self.bytes.extend_from_slice(value.as_bytes());
        Ok(())
    }

    fn url(&mut self, value: &str, allow_empty: bool) -> Result<(), MediaHostError> {
        if value.len() > MAX_URL_BYTES {
            return Err(MediaHostError::LengthExceeded);
        }
        if (value.is_empty() && !allow_empty) || (!value.is_empty() && !valid_url(value)) {
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
        let result = self
            .bytes
            .get(self.offset..end)
            .ok_or(MediaHostError::Truncated)?;
        self.offset = end;
        Ok(result)
    }

    fn u8(&mut self) -> Result<u8, MediaHostError> {
        Ok(self.take(1)?[0])
    }

    fn u32(&mut self) -> Result<u32, MediaHostError> {
        Ok(u32::from_be_bytes(array(self.take(4)?)?))
    }

    fn u16(&mut self) -> Result<u16, MediaHostError> {
        Ok(u16::from_be_bytes(array(self.take(2)?)?))
    }

    fn u64(&mut self) -> Result<u64, MediaHostError> {
        Ok(u64::from_be_bytes(array(self.take(8)?)?))
    }

    fn nonzero_u32(&mut self) -> Result<u32, MediaHostError> {
        let value = self.u32()?;
        (value != 0)
            .then_some(value)
            .ok_or(MediaHostError::InvalidValue)
    }

    fn nonzero_u64(&mut self) -> Result<u64, MediaHostError> {
        let value = self.u64()?;
        (value != 0)
            .then_some(value)
            .ok_or(MediaHostError::InvalidValue)
    }

    fn boolean(&mut self) -> Result<bool, MediaHostError> {
        match self.u8()? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(MediaHostError::InvalidValue),
        }
    }

    fn context(&mut self) -> Result<PlayerContext, MediaHostError> {
        Ok(PlayerContext {
            session_id: self.nonzero_u64()?,
            host_generation: self.nonzero_u64()?,
            tab_id: self.nonzero_u32()?,
            navigation_id: self.nonzero_u64()?,
            tab_generation: self.nonzero_u32()?,
            instance_id: self.nonzero_u64()?,
            source_revision: self.nonzero_u64()?,
        })
    }

    fn page_context(&mut self) -> Result<PlayerPageContext, MediaHostError> {
        Ok(PlayerPageContext {
            session_id: self.nonzero_u64()?,
            host_generation: self.nonzero_u64()?,
            request_id: self.nonzero_u64()?,
            tab_id: self.nonzero_u32()?,
            navigation_id: self.nonzero_u64()?,
            tab_generation: self.nonzero_u32()?,
        })
    }

    fn draft_context(&mut self) -> Result<DraftContext, MediaHostError> {
        Ok(DraftContext {
            session_id: self.nonzero_u64()?,
            host_generation: self.nonzero_u64()?,
            request_id: self.nonzero_u64()?,
            profile_id: self.text(false)?,
            tab_id: self.nonzero_u32()?,
            navigation_id: self.nonzero_u64()?,
            tab_generation: self.nonzero_u32()?,
        })
    }

    fn text(&mut self, allow_empty: bool) -> Result<String, MediaHostError> {
        let length = self.u16()? as usize;
        if length > MAX_ID_BYTES {
            return Err(MediaHostError::LengthExceeded);
        }
        let value = std::str::from_utf8(self.take(length)?)
            .map_err(|_| MediaHostError::InvalidUtf8)?
            .to_owned();
        if !valid_text(&value, allow_empty) {
            return Err(MediaHostError::InvalidValue);
        }
        Ok(value)
    }

    fn origin(&mut self) -> Result<String, MediaHostError> {
        let length = self.u32()? as usize;
        if length > MAX_URL_BYTES {
            return Err(MediaHostError::LengthExceeded);
        }
        let value = std::str::from_utf8(self.take(length)?)
            .map_err(|_| MediaHostError::InvalidUtf8)?
            .to_owned();
        if !valid_origin(&value) {
            return Err(MediaHostError::InvalidValue);
        }
        Ok(value)
    }

    fn url(&mut self, allow_empty: bool) -> Result<String, MediaHostError> {
        let length = self.u32()? as usize;
        if length > MAX_URL_BYTES {
            return Err(MediaHostError::LengthExceeded);
        }
        let value =
            std::str::from_utf8(self.take(length)?).map_err(|_| MediaHostError::InvalidUtf8)?;
        if (value.is_empty() && !allow_empty) || (!value.is_empty() && !valid_url(value)) {
            return Err(MediaHostError::InvalidValue);
        }
        Ok(value.to_owned())
    }
}

fn valid_url(value: &str) -> bool {
    value.len() <= MAX_URL_BYTES
        && !value.chars().any(char::is_control)
        && url::Url::parse(value).is_ok_and(|parsed| {
            matches!(parsed.scheme(), "http" | "https")
                && parsed.host_str().is_some_and(|host| !host.is_empty())
        })
}

fn valid_text(value: &str, allow_empty: bool) -> bool {
    (allow_empty || !value.is_empty())
        && value.len() <= MAX_ID_BYTES
        && !value.chars().any(|c| {
            c.is_control() || matches!(c, '\u{202a}'..='\u{202e}' | '\u{2066}'..='\u{2069}')
        })
}

fn valid_origin(value: &str) -> bool {
    value.is_empty()
        || (value.len() <= MAX_URL_BYTES
            && !value.chars().any(char::is_control)
            && url::Url::parse(value).is_ok_and(|parsed| {
                matches!(parsed.scheme(), "http" | "https")
                    && parsed.origin().ascii_serialization() == value
            }))
}

fn array<const N: usize>(bytes: &[u8]) -> Result<[u8; N], MediaHostError> {
    bytes.try_into().map_err(|_| MediaHostError::Truncated)
}
