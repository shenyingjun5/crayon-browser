//! Transport-independent CAAP wire guard (AGT-12A).
//!
//! This module owns everything between a raw byte stream and the CAAP
//! envelope decoder: length-prefixed framing with a hard byte cap,
//! single-client admission, per-client token-bucket rate limiting,
//! bounded request-id replay rejection, malformed-frame strike
//! disconnect and idempotent stop.  OS endpoint binding (Windows named
//! pipe ACL, macOS UDS peer credentials) lives in the platform adapters
//! behind `crayon-platform-api::LocalAgentIpcEndpoint` and is out of
//! scope here.
//!
//! All state is synchronous and clock-injected; frames are never
//! logged, echoed or interpreted beyond length and request id.

use std::collections::VecDeque;
use std::error::Error;
use std::fmt::{Display, Formatter};
use std::io::{Read, Write};

use crayon_domain::{AgentCapability, CaapError};
use crayon_ipc_schema::{
    CaapCancel, CaapErrorReply, CaapHello, CaapRequest, CaapWelcome, SchemaVersion,
};
use crayon_platform_api::local_agent_ipc::{
    LocalAgentIpcConnection, LocalAgentIpcEndpoint, LocalAgentIpcError,
};

/// Frame header: 4-byte big-endian payload length.
pub const FRAME_HEADER_BYTES: usize = 4;

/// Hard per-frame payload cap (oversize frames are rejected, never
/// buffered).
pub const MAX_FRAME_BYTES: usize = 65_536;

/// Token-bucket capacity: bursts up to this many frames pass instantly.
pub const RATE_BURST: u32 = 32;

/// Token refill interval: one token per this many milliseconds.
pub const RATE_INTERVAL_MS: u64 = 100;

/// Malformed/oversize frames tolerated before the client is dropped.
pub const MAX_STRIKES: u32 = 8;

/// Bounded replay window: request ids remembered per client.
pub const MAX_SEEN_IDS: usize = 512;

/// Bounded stack buffer for each blocking read. FrameCodec owns any
/// legal partial frame across reads.
pub const CONNECTION_READ_BYTES: usize = 8 * 1024;

/// Transport failure.  Variants are stable and carry no client data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TransportError {
    /// The frame payload exceeds `MAX_FRAME_BYTES`.
    FrameTooLarge,
    /// The header or frame layout is malformed.
    FrameMalformed,
    /// A second client tried to bind while one is active.
    ClientBound,
    /// The rate budget is exhausted; the frame was shed.
    RateLimited,
    /// The request id was already seen on this connection.
    Replayed,
    /// Too many protocol violations; the client is dropped.
    StrikesExceeded,
    /// The transport is stopped; no client is bound.
    Stopped,
}

impl Display for TransportError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::FrameTooLarge => "frame exceeds size limit",
            Self::FrameMalformed => "frame malformed",
            Self::ClientBound => "another client already holds the transport",
            Self::RateLimited => "rate limit exceeded",
            Self::Replayed => "request id replayed",
            Self::StrikesExceeded => "too many protocol violations",
            Self::Stopped => "transport stopped",
        };
        formatter.write_str(message)
    }
}

impl Error for TransportError {}

impl TransportError {
    /// Stable mapping onto the closed CAAP error codes.
    #[must_use]
    pub const fn to_caap_error(self) -> crayon_domain::CaapError {
        use crayon_domain::CaapError;
        match self {
            Self::FrameTooLarge => CaapError::InvalidMessage,
            Self::FrameMalformed => CaapError::InvalidMessage,
            Self::ClientBound => CaapError::Unauthorized,
            Self::RateLimited => CaapError::QueueFull,
            Self::Replayed => CaapError::InvalidMessage,
            Self::StrikesExceeded => CaapError::Unauthorized,
            Self::Stopped => CaapError::Unauthorized,
        }
    }
}

/// One decoded outcome of feeding bytes to the frame codec.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum DecodedFrame {
    /// A complete frame payload.
    Complete(Vec<u8>),
    /// More bytes are needed; nothing was produced.
    Incomplete,
    /// The pending frame exceeds the cap; the stream position advanced
    /// past the header and the payload must be treated as poisoned.
    Oversize { declared: u32 },
}

/// Streaming length-prefixed frame decoder.
#[derive(Debug)]
pub struct FrameCodec {
    buffer: Vec<u8>,
}

impl FrameCodec {
    #[must_use]
    pub fn new() -> Self {
        Self { buffer: Vec::new() }
    }

    /// Appends `chunk` and tries to decode one frame.  Callers repeat
    /// until [`DecodedFrame::Incomplete`] before feeding more bytes.
    pub fn feed(&mut self, chunk: &[u8]) -> Result<DecodedFrame, TransportError> {
        if self.buffer.len() + chunk.len() > MAX_FRAME_BYTES * 2 {
            // The pending buffer itself drifted past any legal frame;
            // fail closed instead of growing without bound.
            return Err(TransportError::FrameMalformed);
        }
        self.buffer.extend_from_slice(chunk);
        self.take()
    }

    /// Decodes the next frame out of already-buffered bytes.
    pub fn take(&mut self) -> Result<DecodedFrame, TransportError> {
        if self.buffer.len() < FRAME_HEADER_BYTES {
            return Ok(DecodedFrame::Incomplete);
        }
        let declared = u32::from_be_bytes([
            self.buffer[0],
            self.buffer[1],
            self.buffer[2],
            self.buffer[3],
        ]);
        if declared as usize > MAX_FRAME_BYTES {
            // Drop the header so the caller can resynchronize or drop
            // the connection; the payload is poisoned.
            self.buffer.drain(..FRAME_HEADER_BYTES);
            return Ok(DecodedFrame::Oversize { declared });
        }
        let end = FRAME_HEADER_BYTES + declared as usize;
        if self.buffer.len() < end {
            return Ok(DecodedFrame::Incomplete);
        }
        let payload = self.buffer[FRAME_HEADER_BYTES..end].to_vec();
        self.buffer.drain(..end);
        Ok(DecodedFrame::Complete(payload))
    }

    /// Encodes `payload` into a frame.
    #[must_use]
    pub fn encode(payload: &[u8]) -> Vec<u8> {
        let mut frame = (payload.len() as u32).to_be_bytes().to_vec();
        frame.extend_from_slice(payload);
        frame
    }

    #[must_use]
    pub fn pending_bytes(&self) -> usize {
        self.buffer.len()
    }
}

impl Default for FrameCodec {
    fn default() -> Self {
        Self::new()
    }
}

/// Admission and policing state for one transport endpoint.
pub struct TransportGuard {
    client: Option<String>,
    rate_tokens: u32,
    rate_last_ms: u64,
    strikes: u32,
    seen_ids: VecDeque<u64>,
}

impl TransportGuard {
    #[must_use]
    pub fn new() -> Self {
        Self {
            client: None,
            rate_tokens: RATE_BURST,
            rate_last_ms: 0,
            strikes: 0,
            seen_ids: VecDeque::new(),
        }
    }

    /// Binds the first client; a second concurrent client is rejected
    /// (AG-012 single client).
    pub fn bind_client(&mut self, client: &str) -> Result<(), TransportError> {
        match &self.client {
            Some(bound) if bound == client => Ok(()),
            Some(_) => Err(TransportError::ClientBound),
            None => {
                self.client = Some(client.to_owned());
                Ok(())
            }
        }
    }

    /// Releases the bound client; idempotent.
    pub fn disconnect(&mut self) {
        self.client = None;
        self.strikes = 0;
        self.seen_ids.clear();
        self.rate_tokens = RATE_BURST;
    }

    /// Idempotent stop alias: releases the client slot and resets
    /// policing state.
    pub fn stop(&mut self) {
        self.disconnect();
    }

    /// Records a protocol violation; returns `StrikesExceeded` once the
    /// bound client must be dropped (and drops it).
    pub fn strike(&mut self) -> Result<(), TransportError> {
        self.strikes = self.strikes.saturating_add(1);
        if self.strikes >= MAX_STRIKES {
            self.disconnect();
            return Err(TransportError::StrikesExceeded);
        }
        Ok(())
    }

    /// Applies the token bucket to one inbound frame.
    pub fn admit_rate(&mut self, now_ms: u64) -> Result<(), TransportError> {
        if self.client.is_none() {
            return Err(TransportError::Stopped);
        }
        let elapsed = now_ms.saturating_sub(self.rate_last_ms);
        let refill = (elapsed / RATE_INTERVAL_MS) as u32;
        if refill > 0 {
            self.rate_tokens = self.rate_tokens.saturating_add(refill).min(RATE_BURST);
            self.rate_last_ms = now_ms;
        }
        if self.rate_tokens == 0 {
            return Err(TransportError::RateLimited);
        }
        self.rate_tokens -= 1;
        Ok(())
    }

    /// Rejects replayed request ids (bounded window).
    pub fn admit_request_id(&mut self, request_id: u64) -> Result<(), TransportError> {
        if self.client.is_none() {
            return Err(TransportError::Stopped);
        }
        if self.seen_ids.contains(&request_id) {
            return Err(TransportError::Replayed);
        }
        if self.seen_ids.len() >= MAX_SEEN_IDS {
            self.seen_ids.pop_front();
        }
        self.seen_ids.push_back(request_id);
        Ok(())
    }

    #[must_use]
    pub fn bound_client(&self) -> Option<&str> {
        self.client.as_deref()
    }

    #[must_use]
    pub const fn strikes(&self) -> u32 {
        self.strikes
    }
}

impl Default for TransportGuard {
    fn default() -> Self {
        Self::new()
    }
}

/// A validated inbound message after the CAAP handshake.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InboundMessage {
    Request(CaapRequest),
    Cancel(CaapCancel),
}

/// Connection-level failure. Variants never contain frame payloads,
/// client names, parameters, or peer details.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConnectionError {
    Endpoint(LocalAgentIpcError),
    Transport(TransportError),
    Io,
    HandshakeRequired,
    HandshakeRepeated,
    VersionUnsupported,
    InvalidMessage,
    Closed,
}

impl ConnectionError {
    #[must_use]
    pub const fn to_caap_error(self) -> CaapError {
        match self {
            Self::Endpoint(_) | Self::Io | Self::Closed => CaapError::Unauthorized,
            Self::Transport(error) => error.to_caap_error(),
            Self::HandshakeRequired | Self::HandshakeRepeated | Self::InvalidMessage => {
                CaapError::InvalidMessage
            }
            Self::VersionUnsupported => CaapError::VersionUnsupported,
        }
    }
}

impl Display for ConnectionError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::Endpoint(_) => "local endpoint rejected connection",
            Self::Transport(_) => "transport guard rejected frame",
            Self::Io => "local connection io failed",
            Self::HandshakeRequired => "CAAP handshake required",
            Self::HandshakeRepeated => "CAAP handshake already completed",
            Self::VersionUnsupported => "CAAP version unsupported",
            Self::InvalidMessage => "CAAP message invalid",
            Self::Closed => "local connection closed",
        })
    }
}

impl Error for ConnectionError {}

/// One admitted local CAAP connection.
///
/// The platform endpoint verifies the OS peer before returning the byte
/// stream. This runtime then owns framing, handshake ordering, exact
/// schema negotiation, rate limiting, replay rejection and idempotent
/// disconnect. Tool dispatch and grant issuance remain outside the
/// transport boundary.
pub struct CaapConnection<'a> {
    stream: Box<dyn LocalAgentIpcConnection + 'a>,
    codec: FrameCodec,
    guard: TransportGuard,
    supported_schema: SchemaVersion,
    allowed_capabilities: Vec<AgentCapability>,
    handshaken: bool,
    stopped: bool,
}

impl<'a> CaapConnection<'a> {
    /// Accepts a platform-verified peer. The redundant shared gate uses
    /// only already-derived facts and still runs before the first read.
    pub fn accept(
        endpoint: &'a dyn LocalAgentIpcEndpoint,
        supported_schema: SchemaVersion,
        allowed_capabilities: Vec<AgentCapability>,
    ) -> Result<Self, ConnectionError> {
        let stream = endpoint.accept().map_err(ConnectionError::Endpoint)?;
        endpoint
            .admit_peer(stream.peer_identity())
            .map_err(ConnectionError::Endpoint)?;
        Ok(Self::from_stream(
            stream,
            supported_schema,
            allowed_capabilities,
        ))
    }

    /// Builds the protocol runtime around an already admitted stream.
    /// Kept public for deterministic in-memory contract tests and future
    /// desktop assembly; callers must supply only platform-verified
    /// streams.
    #[must_use]
    pub(crate) fn from_stream(
        stream: Box<dyn LocalAgentIpcConnection + 'a>,
        supported_schema: SchemaVersion,
        allowed_capabilities: Vec<AgentCapability>,
    ) -> Self {
        Self {
            stream,
            codec: FrameCodec::new(),
            guard: TransportGuard::new(),
            supported_schema,
            allowed_capabilities,
            handshaken: false,
            stopped: false,
        }
    }

    /// Reads and validates the mandatory first Hello, then writes the
    /// negotiated Welcome using the same bounded frame format.
    pub fn handshake(&mut self, now_ms: u64) -> Result<CaapWelcome, ConnectionError> {
        if self.stopped {
            return Err(ConnectionError::Closed);
        }
        if self.handshaken {
            return Err(ConnectionError::HandshakeRepeated);
        }
        let payload = self.read_frame()?;
        let hello: CaapHello = serde_json::from_slice(&payload)
            .map_err(|_| self.invalid_message(ConnectionError::InvalidMessage))?;
        hello
            .validate()
            .map_err(|_| self.invalid_message(ConnectionError::InvalidMessage))?;
        if hello.schema() != self.supported_schema {
            self.write_error(0, CaapError::VersionUnsupported)?;
            self.stop()?;
            return Err(ConnectionError::VersionUnsupported);
        }
        self.guard
            .bind_client(hello.client())
            .map_err(ConnectionError::Transport)?;
        self.guard
            .admit_rate(now_ms)
            .map_err(ConnectionError::Transport)?;
        let granted = hello
            .capabilities()
            .iter()
            .copied()
            .filter(|capability| self.allowed_capabilities.contains(capability))
            .collect();
        let welcome = CaapWelcome::new(self.supported_schema, granted)
            .map_err(|_| ConnectionError::InvalidMessage)?;
        self.write_json_frame(&welcome)?;
        self.handshaken = true;
        Ok(welcome)
    }

    /// Reads the next validated request or cancel. No message can reach
    /// dispatch before a successful Hello/Welcome exchange.
    pub fn receive(&mut self, now_ms: u64) -> Result<InboundMessage, ConnectionError> {
        if self.stopped {
            return Err(ConnectionError::Closed);
        }
        if !self.handshaken {
            return Err(ConnectionError::HandshakeRequired);
        }
        let payload = self.read_frame()?;
        self.guard
            .admit_rate(now_ms)
            .map_err(ConnectionError::Transport)?;

        if let Ok(request) = serde_json::from_slice::<CaapRequest>(&payload) {
            request
                .validate()
                .map_err(|_| self.invalid_message(ConnectionError::InvalidMessage))?;
            self.guard
                .admit_request_id(request.id())
                .map_err(ConnectionError::Transport)?;
            return Ok(InboundMessage::Request(request));
        }
        if let Ok(cancel) = serde_json::from_slice::<CaapCancel>(&payload) {
            return Ok(InboundMessage::Cancel(cancel));
        }
        if serde_json::from_slice::<CaapHello>(&payload).is_ok() {
            return Err(self.invalid_message(ConnectionError::HandshakeRepeated));
        }
        Err(self.invalid_message(ConnectionError::InvalidMessage))
    }

    /// Writes a stable CAAP error reply without exposing diagnostic text.
    pub fn write_error(&mut self, id: u64, error: CaapError) -> Result<(), ConnectionError> {
        self.write_json_frame(&CaapErrorReply::new(id, error))
    }

    /// Releases the client slot and OS connection. Repeated calls are
    /// successful and perform no further IO.
    pub fn stop(&mut self) -> Result<(), ConnectionError> {
        if self.stopped {
            return Ok(());
        }
        self.guard.stop();
        self.stream.close().map_err(ConnectionError::Endpoint)?;
        self.stopped = true;
        Ok(())
    }

    fn read_frame(&mut self) -> Result<Vec<u8>, ConnectionError> {
        loop {
            match self.codec.take().map_err(ConnectionError::Transport)? {
                DecodedFrame::Complete(payload) => return Ok(payload),
                DecodedFrame::Oversize { .. } => {
                    let _ = self.guard.strike();
                    let _ = self.stop();
                    return Err(ConnectionError::Transport(TransportError::FrameTooLarge));
                }
                DecodedFrame::Incomplete => {}
            }
            let mut buffer = [0u8; CONNECTION_READ_BYTES];
            let read = self
                .stream
                .read(&mut buffer)
                .map_err(|_| self.io_failure())?;
            if read == 0 {
                let _ = self.stop();
                return Err(ConnectionError::Closed);
            }
            match self
                .codec
                .feed(&buffer[..read])
                .map_err(ConnectionError::Transport)?
            {
                DecodedFrame::Complete(payload) => return Ok(payload),
                DecodedFrame::Oversize { .. } => {
                    let _ = self.guard.strike();
                    let _ = self.stop();
                    return Err(ConnectionError::Transport(TransportError::FrameTooLarge));
                }
                DecodedFrame::Incomplete => {}
            }
        }
    }

    fn write_json_frame<T>(&mut self, value: &T) -> Result<(), ConnectionError>
    where
        T: serde::Serialize,
    {
        let payload = serde_json::to_vec(value).map_err(|_| ConnectionError::InvalidMessage)?;
        if payload.len() > MAX_FRAME_BYTES {
            return Err(ConnectionError::Transport(TransportError::FrameTooLarge));
        }
        self.stream
            .write_all(&FrameCodec::encode(&payload))
            .map_err(|_| self.io_failure())?;
        self.stream.flush().map_err(|_| self.io_failure())
    }

    fn invalid_message(&mut self, error: ConnectionError) -> ConnectionError {
        if self.guard.strike().is_err() {
            let _ = self.stop();
        }
        error
    }

    fn io_failure(&mut self) -> ConnectionError {
        let _ = self.stop();
        ConnectionError::Io
    }
}

impl Drop for CaapConnection<'_> {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

#[cfg(test)]
#[path = "transport_tests.rs"]
mod tests;
