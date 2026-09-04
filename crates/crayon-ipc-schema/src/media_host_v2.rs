//! MHV2 handshake wire only. Decoding/matching does not authenticate a peer,
//! enable capabilities, or authorize commands. MHV1 remains a separate codec.
use crate::MediaHostError;

const MAGIC: &[u8; 4] = b"MHV2";
const VERSION: u16 = 2;
const HEADER_BYTES: usize = 8;
pub const HANDSHAKE_BYTES: usize = 34;
pub const MAX_FRAME_BYTES: u32 = 16 * 1024;
pub const MAX_PAGE_ITEMS: u16 = 16;
pub const CAP_MEDIA_READ: u32 = 1;
pub const CAP_DRAFT: u32 = 2;
pub const CAP_CONNECT: u32 = 4;
pub const CAP_STOP: u32 = 8;
const KNOWN_CAPABILITIES: u32 = CAP_MEDIA_READ | CAP_DRAFT | CAP_CONNECT | CAP_STOP;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Kind {
    Hello = 1,
    Welcome = 2,
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

fn array<const N: usize>(bytes: &[u8]) -> Result<[u8; N], MediaHostError> {
    bytes.try_into().map_err(|_| MediaHostError::Truncated)
}
