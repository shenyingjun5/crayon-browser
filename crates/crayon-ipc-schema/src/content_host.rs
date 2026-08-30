//! CNT-18a: deterministic, bounded v1 messages exchanged between the
//! Browser-verified page snapshot gateway and the Rust content host.
//! This module owns no transport, process, page state or extraction logic.

use std::error::Error;
use std::fmt::{Display, Formatter};

const MAGIC: &[u8; 4] = b"CHV1";
const VERSION: u16 = 1;
const HEADER_BYTES: usize = 8;
pub const MAX_CONTENT_HOST_FRAME_BYTES: usize = 65_536;
pub const MAX_CONTENT_HOST_FACTS: usize = 64;
pub const MAX_CONTENT_HOST_MARKDOWN_BYTES: usize = 60 * 1024;
const MAX_ID_BYTES: usize = 128;
const MAX_URL_BYTES: usize = 2048;
const MAX_TITLE_BYTES: usize = 512;
const MAX_FACT_TEXT_BYTES: usize = 32 * 1024;
const MAX_LANGUAGE_BYTES: usize = 32;
const MAX_TABLE_CELLS: usize = 256 * 32;
const MAX_TABLE_CELL_BYTES: usize = 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum ContentHostMode {
    Standard = 0,
    Compact = 1,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum ContentHostFactKind {
    Heading = 0,
    Paragraph = 1,
    ListItem = 2,
    Link = 3,
    Image = 4,
    Table = 5,
    CodeBlock = 6,
    Divider = 7,
    Quote = 8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum ContentHostTerminalStatus {
    Completed = 0,
    Cancelled = 1,
    StaleNavigation = 2,
    Rejected = 3,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum EngineErrorCode {
    None = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    AlreadyExists = 3,
    NotFound = 4,
    StaleNavigation = 5,
    Unsupported = 6,
    CapacityExceeded = 7,
    NavigationFailed = 8,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum ContentHostErrorCode {
    InvalidMessage = 0,
    InvalidState = 1,
    StaleNavigation = 2,
    CapacityExceeded = 3,
    ExtractionFailed = 4,
    MarkdownFailed = 5,
    Cancelled = 6,
    HostUnavailable = 7,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ContentHostFact {
    pub kind: ContentHostFactKind,
    pub text: String,
    pub url: Option<String>,
    pub language: Option<String>,
    pub level: u8,
    pub depth: u8,
    pub ordered: bool,
    pub ordinal: Option<u32>,
    pub table_columns: u16,
    pub table_cells: Vec<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ContentHostMessage {
    Begin {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
        mode: ContentHostMode,
        url: String,
        title: String,
    },
    FactBatch {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
        sequence: u32,
        facts: Vec<ContentHostFact>,
    },
    Terminal {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
        status: ContentHostTerminalStatus,
        error: EngineErrorCode,
    },
    Cancel {
        request_id: String,
    },
    Navigation {
        tab_id: String,
        navigation_id: u64,
        generation: u64,
    },
    CloseTab {
        tab_id: String,
    },
    Shutdown,
    MarkdownChunk {
        request_id: String,
        tab_id: String,
        navigation_id: u64,
        generation: u64,
        sequence: u32,
        completed: bool,
        markdown: String,
    },
    ErrorReply {
        request_id: String,
        code: ContentHostErrorCode,
    },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContentHostError {
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

impl Display for ContentHostError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::FrameTooLarge => "content-host frame exceeds size limit",
            Self::InvalidMagic => "content-host magic rejected",
            Self::UnsupportedVersion => "content-host version rejected",
            Self::UnknownKind => "content-host message kind rejected",
            Self::InvalidFlags => "content-host flags rejected",
            Self::Truncated => "content-host frame truncated",
            Self::TrailingBytes => "content-host frame has trailing bytes",
            Self::InvalidUtf8 => "content-host string is not UTF-8",
            Self::InvalidValue => "content-host value rejected",
            Self::LengthExceeded => "content-host field exceeds size limit",
        })
    }
}

impl Error for ContentHostError {}

#[repr(u8)]
enum Kind {
    Begin = 1,
    FactBatch = 2,
    Terminal = 3,
    Cancel = 4,
    Navigation = 5,
    CloseTab = 6,
    Shutdown = 7,
    MarkdownChunk = 8,
    ErrorReply = 9,
}

pub fn encode_content_host_message(
    message: &ContentHostMessage,
) -> Result<Vec<u8>, ContentHostError> {
    let mut writer = Writer::new(kind_of(message));
    match message {
        ContentHostMessage::Begin {
            request_id,
            tab_id,
            navigation_id,
            generation,
            mode,
            url,
            title,
        } => {
            if title.contains(['\n', '\r', '\t']) {
                return Err(ContentHostError::InvalidValue);
            }
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
            writer.u8(*mode as u8);
            writer.string(url, MAX_URL_BYTES, false)?;
            writer.string(title, MAX_TITLE_BYTES, false)?;
        }
        ContentHostMessage::FactBatch {
            request_id,
            tab_id,
            navigation_id,
            generation,
            sequence,
            facts,
        } => {
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
            writer.u32(*sequence);
            if facts.is_empty() || facts.len() > MAX_CONTENT_HOST_FACTS {
                return Err(ContentHostError::InvalidValue);
            }
            writer.u16(facts.len() as u16);
            for fact in facts {
                encode_fact(&mut writer, fact)?;
            }
        }
        ContentHostMessage::Terminal {
            request_id,
            tab_id,
            navigation_id,
            generation,
            status,
            error,
        } => {
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
            if (*status == ContentHostTerminalStatus::Completed)
                != (*error == EngineErrorCode::None)
            {
                return Err(ContentHostError::InvalidValue);
            }
            writer.u8(*status as u8);
            writer.u8(*error as u8);
        }
        ContentHostMessage::Cancel { request_id } => writer.id(request_id)?,
        ContentHostMessage::Navigation {
            tab_id,
            navigation_id,
            generation,
        } => {
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
        }
        ContentHostMessage::CloseTab { tab_id } => writer.tab_id(tab_id)?,
        ContentHostMessage::Shutdown => {}
        ContentHostMessage::MarkdownChunk {
            request_id,
            tab_id,
            navigation_id,
            generation,
            sequence,
            completed,
            markdown,
        } => {
            writer.id(request_id)?;
            writer.tab_id(tab_id)?;
            writer.nonzero_u64(*navigation_id)?;
            writer.nonzero_u64(*generation)?;
            writer.u32(*sequence);
            writer.boolean(*completed);
            writer.string(markdown, MAX_CONTENT_HOST_MARKDOWN_BYTES, true)?;
        }
        ContentHostMessage::ErrorReply { request_id, code } => {
            writer.id(request_id)?;
            writer.u8(*code as u8);
        }
    }
    writer.finish()
}

pub fn decode_content_host_message(bytes: &[u8]) -> Result<ContentHostMessage, ContentHostError> {
    if bytes.len() > MAX_CONTENT_HOST_FRAME_BYTES {
        return Err(ContentHostError::FrameTooLarge);
    }
    if bytes.len() < HEADER_BYTES {
        return Err(ContentHostError::Truncated);
    }
    if &bytes[..4] != MAGIC {
        return Err(ContentHostError::InvalidMagic);
    }
    let version = u16::from_be_bytes([bytes[4], bytes[5]]);
    if version != VERSION {
        return Err(ContentHostError::UnsupportedVersion);
    }
    if bytes[7] != 0 {
        return Err(ContentHostError::InvalidFlags);
    }
    let mut reader = Reader::new(&bytes[HEADER_BYTES..]);
    let message = match bytes[6] {
        1 => {
            let request_id = reader.id()?;
            let tab_id = reader.tab_id()?;
            let navigation_id = reader.nonzero_u64()?;
            let generation = reader.nonzero_u64()?;
            let mode = mode(reader.u8()?)?;
            let url = reader.string(MAX_URL_BYTES, false)?;
            let title = reader.string(MAX_TITLE_BYTES, false)?;
            if title.contains(['\n', '\r', '\t']) {
                return Err(ContentHostError::InvalidValue);
            }
            ContentHostMessage::Begin {
                request_id,
                tab_id,
                navigation_id,
                generation,
                mode,
                url,
                title,
            }
        }
        2 => {
            let request_id = reader.id()?;
            let tab_id = reader.tab_id()?;
            let navigation_id = reader.nonzero_u64()?;
            let generation = reader.nonzero_u64()?;
            let sequence = reader.u32()?;
            let count = reader.u16()? as usize;
            if count == 0 || count > MAX_CONTENT_HOST_FACTS {
                return Err(ContentHostError::InvalidValue);
            }
            let mut facts = Vec::with_capacity(count);
            for _ in 0..count {
                facts.push(decode_fact(&mut reader)?);
            }
            ContentHostMessage::FactBatch {
                request_id,
                tab_id,
                navigation_id,
                generation,
                sequence,
                facts,
            }
        }
        3 => {
            let request_id = reader.id()?;
            let tab_id = reader.tab_id()?;
            let navigation_id = reader.nonzero_u64()?;
            let generation = reader.nonzero_u64()?;
            let status = terminal_status(reader.u8()?)?;
            let error = engine_error(reader.u8()?)?;
            if (status == ContentHostTerminalStatus::Completed) != (error == EngineErrorCode::None)
            {
                return Err(ContentHostError::InvalidValue);
            }
            ContentHostMessage::Terminal {
                request_id,
                tab_id,
                navigation_id,
                generation,
                status,
                error,
            }
        }
        4 => ContentHostMessage::Cancel {
            request_id: reader.id()?,
        },
        5 => ContentHostMessage::Navigation {
            tab_id: reader.tab_id()?,
            navigation_id: reader.nonzero_u64()?,
            generation: reader.nonzero_u64()?,
        },
        6 => ContentHostMessage::CloseTab {
            tab_id: reader.tab_id()?,
        },
        7 => ContentHostMessage::Shutdown,
        8 => ContentHostMessage::MarkdownChunk {
            request_id: reader.id()?,
            tab_id: reader.tab_id()?,
            navigation_id: reader.nonzero_u64()?,
            generation: reader.nonzero_u64()?,
            sequence: reader.u32()?,
            completed: reader.boolean()?,
            markdown: reader.string(MAX_CONTENT_HOST_MARKDOWN_BYTES, true)?,
        },
        9 => ContentHostMessage::ErrorReply {
            request_id: reader.id()?,
            code: host_error(reader.u8()?)?,
        },
        _ => return Err(ContentHostError::UnknownKind),
    };
    if !reader.is_empty() {
        return Err(ContentHostError::TrailingBytes);
    }
    Ok(message)
}

fn kind_of(message: &ContentHostMessage) -> Kind {
    match message {
        ContentHostMessage::Begin { .. } => Kind::Begin,
        ContentHostMessage::FactBatch { .. } => Kind::FactBatch,
        ContentHostMessage::Terminal { .. } => Kind::Terminal,
        ContentHostMessage::Cancel { .. } => Kind::Cancel,
        ContentHostMessage::Navigation { .. } => Kind::Navigation,
        ContentHostMessage::CloseTab { .. } => Kind::CloseTab,
        ContentHostMessage::Shutdown => Kind::Shutdown,
        ContentHostMessage::MarkdownChunk { .. } => Kind::MarkdownChunk,
        ContentHostMessage::ErrorReply { .. } => Kind::ErrorReply,
    }
}

fn encode_fact(writer: &mut Writer, fact: &ContentHostFact) -> Result<(), ContentHostError> {
    validate_fact(fact)?;
    writer.u8(fact.kind as u8);
    writer.string(&fact.text, MAX_FACT_TEXT_BYTES, true)?;
    writer.optional_string(fact.url.as_deref(), MAX_URL_BYTES)?;
    writer.optional_string(fact.language.as_deref(), MAX_LANGUAGE_BYTES)?;
    writer.u8(fact.level);
    writer.u8(fact.depth);
    writer.boolean(fact.ordered);
    writer.u32(fact.ordinal.unwrap_or(0));
    writer.u16(fact.table_columns);
    writer.u16(fact.table_cells.len() as u16);
    for cell in &fact.table_cells {
        writer.string(cell, MAX_TABLE_CELL_BYTES, true)?;
    }
    Ok(())
}

fn decode_fact(reader: &mut Reader<'_>) -> Result<ContentHostFact, ContentHostError> {
    let fact = ContentHostFact {
        kind: fact_kind(reader.u8()?)?,
        text: reader.string(MAX_FACT_TEXT_BYTES, true)?,
        url: reader.optional_string(MAX_URL_BYTES)?,
        language: reader.optional_string(MAX_LANGUAGE_BYTES)?,
        level: reader.u8()?,
        depth: reader.u8()?,
        ordered: reader.boolean()?,
        ordinal: match reader.u32()? {
            0 => None,
            value => Some(value),
        },
        table_columns: reader.u16()?,
        table_cells: {
            let count = reader.u16()? as usize;
            if count > MAX_TABLE_CELLS {
                return Err(ContentHostError::LengthExceeded);
            }
            let mut cells = Vec::with_capacity(count);
            for _ in 0..count {
                cells.push(reader.string(MAX_TABLE_CELL_BYTES, true)?);
            }
            cells
        },
    };
    validate_fact(&fact)?;
    Ok(fact)
}

fn validate_fact(fact: &ContentHostFact) -> Result<(), ContentHostError> {
    let base = fact.url.is_none()
        && fact.language.is_none()
        && fact.level == 0
        && fact.depth == 0
        && !fact.ordered
        && fact.ordinal.is_none()
        && fact.table_columns == 0
        && fact.table_cells.is_empty();
    let nonempty_text = !fact.text.is_empty() && fact.text.len() <= MAX_FACT_TEXT_BYTES;
    let valid = match fact.kind {
        ContentHostFactKind::Heading => {
            nonempty_text
                && (1..=6).contains(&fact.level)
                && fact.url.is_none()
                && fact.language.is_none()
                && fact.depth == 0
                && !fact.ordered
                && fact.ordinal.is_none()
                && fact.table_columns == 0
                && fact.table_cells.is_empty()
        }
        ContentHostFactKind::Paragraph | ContentHostFactKind::Quote => nonempty_text && base,
        ContentHostFactKind::ListItem => {
            nonempty_text
                && fact.url.is_none()
                && fact.language.is_none()
                && fact.level == 0
                && (1..=8).contains(&fact.depth)
                && (fact.ordered == fact.ordinal.is_some())
                && fact.table_columns == 0
                && fact.table_cells.is_empty()
        }
        ContentHostFactKind::Link => {
            nonempty_text
                && fact.url.is_some()
                && fact.language.is_none()
                && fact.level == 0
                && fact.depth == 0
                && !fact.ordered
                && fact.ordinal.is_none()
                && fact.table_columns == 0
                && fact.table_cells.is_empty()
        }
        ContentHostFactKind::Image => {
            fact.url.is_some()
                && fact.language.is_none()
                && fact.level == 0
                && fact.depth == 0
                && !fact.ordered
                && fact.ordinal.is_none()
                && fact.table_columns == 0
                && fact.table_cells.is_empty()
        }
        ContentHostFactKind::Table => {
            fact.text.is_empty()
                && fact.url.is_none()
                && fact.language.is_none()
                && fact.level == 0
                && fact.depth == 0
                && !fact.ordered
                && fact.ordinal.is_none()
                && (1..=32).contains(&fact.table_columns)
                && !fact.table_cells.is_empty()
                && fact.table_cells.len() <= MAX_TABLE_CELLS
                && fact.table_cells.len() % fact.table_columns as usize == 0
                && fact.table_cells.len() / fact.table_columns as usize <= 256
        }
        ContentHostFactKind::CodeBlock => {
            nonempty_text
                && fact.url.is_none()
                && fact.level == 0
                && fact.depth == 0
                && !fact.ordered
                && fact.ordinal.is_none()
                && fact.table_columns == 0
                && fact.table_cells.is_empty()
                && fact.language.as_deref().is_none_or(valid_language)
        }
        ContentHostFactKind::Divider => fact.text.is_empty() && base,
    };
    if !valid
        || !valid_text(&fact.text)
        || fact.url.as_deref().is_some_and(|v| !valid_text(v))
        || fact.table_cells.iter().any(|v| !valid_text(v))
    {
        return Err(ContentHostError::InvalidValue);
    }
    Ok(())
}

fn valid_text(value: &str) -> bool {
    value.chars().all(|c| {
        c == '\n' || c == '\t' || (!c.is_control() && !(0x80..=0x9f).contains(&(c as u32)))
    })
}

fn valid_language(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= MAX_LANGUAGE_BYTES
        && value.bytes().all(|b| {
            b.is_ascii_lowercase() || b.is_ascii_digit() || matches!(b, b'_' | b'+' | b'-')
        })
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
    fn finish(self) -> Result<Vec<u8>, ContentHostError> {
        if self.bytes.len() > MAX_CONTENT_HOST_FRAME_BYTES {
            Err(ContentHostError::FrameTooLarge)
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
    fn nonzero_u64(&mut self, value: u64) -> Result<(), ContentHostError> {
        if value == 0 {
            return Err(ContentHostError::InvalidValue);
        }
        self.bytes.extend_from_slice(&value.to_be_bytes());
        Ok(())
    }
    fn boolean(&mut self, value: bool) {
        self.u8(u8::from(value));
    }
    fn id(&mut self, value: &str) -> Result<(), ContentHostError> {
        if !valid_id(value) {
            return Err(ContentHostError::InvalidValue);
        }
        self.string(value, MAX_ID_BYTES, false)
    }
    fn tab_id(&mut self, value: &str) -> Result<(), ContentHostError> {
        if !valid_tab_id(value) {
            return Err(ContentHostError::InvalidValue);
        }
        self.string(value, MAX_ID_BYTES, false)
    }
    fn optional_string(&mut self, value: Option<&str>, max: usize) -> Result<(), ContentHostError> {
        self.string(value.unwrap_or_default(), max, true)
    }
    fn string(
        &mut self,
        value: &str,
        max: usize,
        allow_empty: bool,
    ) -> Result<(), ContentHostError> {
        if value.len() > max {
            return Err(ContentHostError::LengthExceeded);
        }
        if (!allow_empty && value.is_empty()) || !valid_text(value) {
            return Err(ContentHostError::InvalidValue);
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
    fn take(&mut self, count: usize) -> Result<&'a [u8], ContentHostError> {
        let end = self
            .offset
            .checked_add(count)
            .ok_or(ContentHostError::Truncated)?;
        let value = self
            .bytes
            .get(self.offset..end)
            .ok_or(ContentHostError::Truncated)?;
        self.offset = end;
        Ok(value)
    }
    fn u8(&mut self) -> Result<u8, ContentHostError> {
        Ok(self.take(1)?[0])
    }
    fn u16(&mut self) -> Result<u16, ContentHostError> {
        let v = self.take(2)?;
        Ok(u16::from_be_bytes([v[0], v[1]]))
    }
    fn u32(&mut self) -> Result<u32, ContentHostError> {
        let v = self.take(4)?;
        Ok(u32::from_be_bytes([v[0], v[1], v[2], v[3]]))
    }
    fn nonzero_u64(&mut self) -> Result<u64, ContentHostError> {
        let v = self.take(8)?;
        let value = u64::from_be_bytes(v.try_into().map_err(|_| ContentHostError::Truncated)?);
        if value == 0 {
            Err(ContentHostError::InvalidValue)
        } else {
            Ok(value)
        }
    }
    fn boolean(&mut self) -> Result<bool, ContentHostError> {
        match self.u8()? {
            0 => Ok(false),
            1 => Ok(true),
            _ => Err(ContentHostError::InvalidValue),
        }
    }
    fn id(&mut self) -> Result<String, ContentHostError> {
        let value = self.string(MAX_ID_BYTES, false)?;
        if !valid_id(&value) {
            return Err(ContentHostError::InvalidValue);
        }
        Ok(value)
    }
    fn tab_id(&mut self) -> Result<String, ContentHostError> {
        let value = self.string(MAX_ID_BYTES, false)?;
        if !valid_tab_id(&value) {
            return Err(ContentHostError::InvalidValue);
        }
        Ok(value)
    }
    fn optional_string(&mut self, max: usize) -> Result<Option<String>, ContentHostError> {
        let v = self.string(max, true)?;
        Ok((!v.is_empty()).then_some(v))
    }
    fn string(&mut self, max: usize, allow_empty: bool) -> Result<String, ContentHostError> {
        let len = self.u32()? as usize;
        if len > max {
            return Err(ContentHostError::LengthExceeded);
        }
        let raw = self.take(len)?;
        let value = std::str::from_utf8(raw).map_err(|_| ContentHostError::InvalidUtf8)?;
        if (!allow_empty && value.is_empty()) || !valid_text(value) {
            return Err(ContentHostError::InvalidValue);
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

macro_rules! closed_enum { ($fn:ident, $ty:ty, {$($raw:literal => $value:expr),+ $(,)?}) => { fn $fn(raw:u8)->Result<$ty,ContentHostError>{match raw{$($raw=>Ok($value),)+ _=>Err(ContentHostError::InvalidValue)}} }; }
closed_enum!(mode, ContentHostMode, {0=>ContentHostMode::Standard,1=>ContentHostMode::Compact});
closed_enum!(fact_kind, ContentHostFactKind, {0=>ContentHostFactKind::Heading,1=>ContentHostFactKind::Paragraph,2=>ContentHostFactKind::ListItem,3=>ContentHostFactKind::Link,4=>ContentHostFactKind::Image,5=>ContentHostFactKind::Table,6=>ContentHostFactKind::CodeBlock,7=>ContentHostFactKind::Divider,8=>ContentHostFactKind::Quote});
closed_enum!(terminal_status, ContentHostTerminalStatus, {0=>ContentHostTerminalStatus::Completed,1=>ContentHostTerminalStatus::Cancelled,2=>ContentHostTerminalStatus::StaleNavigation,3=>ContentHostTerminalStatus::Rejected});
closed_enum!(engine_error, EngineErrorCode, {0=>EngineErrorCode::None,1=>EngineErrorCode::InvalidArgument,2=>EngineErrorCode::InvalidState,3=>EngineErrorCode::AlreadyExists,4=>EngineErrorCode::NotFound,5=>EngineErrorCode::StaleNavigation,6=>EngineErrorCode::Unsupported,7=>EngineErrorCode::CapacityExceeded,8=>EngineErrorCode::NavigationFailed});
closed_enum!(host_error, ContentHostErrorCode, {0=>ContentHostErrorCode::InvalidMessage,1=>ContentHostErrorCode::InvalidState,2=>ContentHostErrorCode::StaleNavigation,3=>ContentHostErrorCode::CapacityExceeded,4=>ContentHostErrorCode::ExtractionFailed,5=>ContentHostErrorCode::MarkdownFailed,6=>ContentHostErrorCode::Cancelled,7=>ContentHostErrorCode::HostUnavailable});
