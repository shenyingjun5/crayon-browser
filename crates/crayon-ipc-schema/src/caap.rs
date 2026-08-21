//! CAAP v1 wire messages (AGT-01): handshake, the six-envelope set and
//! their bounds.  Transport, the tool registry and session state machines
//! belong to later tasks; this module freezes only the wire contract.
//!
//! Bounds and validation:
//! - client/tool names and idempotency keys are closed-charset tokens
//!   (`[a-z0-9_.:-]`, at most 64 bytes);
//! - request parameters are a bounded string map (16 entries, key <= 32
//!   bytes, value <= 1024 bytes); parameter values must never carry
//!   credentials — receipts and diagnostics redact on their own paths;
//! - stream chunk payloads are at most 4096 bytes;
//! - every message denies unknown fields and carries the non-zero schema
//!   version where negotiation applies.

use crate::SchemaVersion;
use crayon_domain::{AgentCapability, AgentTarget, CaapError};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum length of a client or tool name and of an idempotency key.
pub const MAX_CAAP_TOKEN_LEN: usize = 64;

/// Maximum number of request parameters.
pub const MAX_CAAP_PARAMS: usize = 16;

/// Maximum length of a request parameter key, in bytes.
pub const MAX_CAAP_PARAM_KEY_LEN: usize = 32;

/// Maximum length of a request parameter value, in bytes.
pub const MAX_CAAP_PARAM_VALUE_LEN: usize = 1024;

/// Maximum payload of one stream chunk, in bytes.
pub const MAX_CAAP_CHUNK_BYTES: usize = 4096;

/// Maximum number of capabilities carried by one handshake message.
pub const MAX_CAAP_CAPABILITIES: usize = 8;

/// CAAP message validation failure.  Variants are stable and carry no
/// payload data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CaapSchemaError {
    /// A token is empty, too long or uses characters outside the closed
    /// set.
    InvalidToken,
    /// Too many capabilities on a handshake message.
    TooManyCapabilities,
    /// Too many request parameters.
    TooManyParams,
    /// A parameter key violates shape or bounds.
    InvalidParamKey,
    /// A parameter value exceeds the length bound.
    ParamValueTooLong,
    /// A stream chunk payload exceeds the size bound.
    ChunkTooLarge,
}

impl Display for CaapSchemaError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidToken => "token violates shape or bounds",
            Self::TooManyCapabilities => "handshake carries too many capabilities",
            Self::TooManyParams => "request carries too many parameters",
            Self::InvalidParamKey => "parameter key violates shape or bounds",
            Self::ParamValueTooLong => "parameter value exceeds the length bound",
            Self::ChunkTooLarge => "stream chunk exceeds the size bound",
        };
        formatter.write_str(message)
    }
}

impl Error for CaapSchemaError {}

/// Reports whether `token` is non-empty, within `MAX_CAAP_TOKEN_LEN` and
/// uses only the closed character set `[a-z0-9_.:-]`.
fn is_token(token: &str) -> bool {
    !token.is_empty()
        && token.len() <= MAX_CAAP_TOKEN_LEN
        && token.bytes().all(|byte| {
            byte.is_ascii_lowercase()
                || byte.is_ascii_digit()
                || matches!(byte, b'_' | b'.' | b':' | b'-')
        })
}

fn validate_capabilities(capabilities: &[AgentCapability]) -> Result<(), CaapSchemaError> {
    if capabilities.len() > MAX_CAAP_CAPABILITIES {
        return Err(CaapSchemaError::TooManyCapabilities);
    }
    Ok(())
}

fn validate_params(params: &BTreeMap<String, String>) -> Result<(), CaapSchemaError> {
    if params.len() > MAX_CAAP_PARAMS {
        return Err(CaapSchemaError::TooManyParams);
    }
    for (key, value) in params {
        if key.is_empty()
            || key.len() > MAX_CAAP_PARAM_KEY_LEN
            || !key.bytes().all(|byte| {
                byte.is_ascii_lowercase()
                    || byte.is_ascii_digit()
                    || matches!(byte, b'_' | b'.' | b':' | b'-')
            })
        {
            return Err(CaapSchemaError::InvalidParamKey);
        }
        if value.len() > MAX_CAAP_PARAM_VALUE_LEN {
            return Err(CaapSchemaError::ParamValueTooLong);
        }
    }
    Ok(())
}

/// Client hello: opens the handshake with requested capabilities.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaapHello {
    schema: SchemaVersion,
    client: String,
    capabilities: Vec<AgentCapability>,
}

impl CaapHello {
    /// Creates a validated hello.
    pub fn new(
        schema: SchemaVersion,
        client: &str,
        capabilities: Vec<AgentCapability>,
    ) -> Result<Self, CaapSchemaError> {
        if !is_token(client) {
            return Err(CaapSchemaError::InvalidToken);
        }
        validate_capabilities(&capabilities)?;
        Ok(Self {
            schema,
            client: client.to_owned(),
            capabilities,
        })
    }

    /// Re-checks a decoded message against the bounds.
    pub fn validate(&self) -> Result<(), CaapSchemaError> {
        if !is_token(&self.client) {
            return Err(CaapSchemaError::InvalidToken);
        }
        validate_capabilities(&self.capabilities)
    }

    #[must_use]
    pub const fn schema(&self) -> SchemaVersion {
        self.schema
    }

    #[must_use]
    pub fn client(&self) -> &str {
        &self.client
    }

    #[must_use]
    pub fn capabilities(&self) -> &[AgentCapability] {
        &self.capabilities
    }
}

/// Server welcome: closes the handshake with the granted capabilities.
/// Carries no session material; grants are issued by the session layer.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaapWelcome {
    schema: SchemaVersion,
    capabilities: Vec<AgentCapability>,
}

impl CaapWelcome {
    /// Creates a validated welcome.
    pub fn new(
        schema: SchemaVersion,
        capabilities: Vec<AgentCapability>,
    ) -> Result<Self, CaapSchemaError> {
        validate_capabilities(&capabilities)?;
        Ok(Self {
            schema,
            capabilities,
        })
    }

    /// Re-checks a decoded message against the bounds.
    pub fn validate(&self) -> Result<(), CaapSchemaError> {
        validate_capabilities(&self.capabilities)
    }

    #[must_use]
    pub const fn schema(&self) -> SchemaVersion {
        self.schema
    }

    #[must_use]
    pub fn capabilities(&self) -> &[AgentCapability] {
        &self.capabilities
    }
}

/// Tool invocation request.  `deadline_ms` is a caller-injected epoch
/// timestamp; the session layer owns cancellation and idempotency
/// semantics.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaapRequest {
    id: u64,
    tool: String,
    target: AgentTarget,
    deadline_ms: u64,
    idempotency_key: String,
    params: BTreeMap<String, String>,
}

impl CaapRequest {
    /// Creates a validated request.
    pub fn new(
        id: u64,
        tool: &str,
        target: AgentTarget,
        deadline_ms: u64,
        idempotency_key: &str,
        params: BTreeMap<String, String>,
    ) -> Result<Self, CaapSchemaError> {
        if !is_token(tool) || !is_token(idempotency_key) {
            return Err(CaapSchemaError::InvalidToken);
        }
        validate_params(&params)?;
        Ok(Self {
            id,
            tool: tool.to_owned(),
            target,
            deadline_ms,
            idempotency_key: idempotency_key.to_owned(),
            params,
        })
    }

    /// Re-checks a decoded request against the bounds.
    pub fn validate(&self) -> Result<(), CaapSchemaError> {
        if !is_token(&self.tool) || !is_token(&self.idempotency_key) {
            return Err(CaapSchemaError::InvalidToken);
        }
        validate_params(&self.params)
    }

    #[must_use]
    pub const fn id(&self) -> u64 {
        self.id
    }

    #[must_use]
    pub fn tool(&self) -> &str {
        &self.tool
    }

    #[must_use]
    pub const fn target(&self) -> &AgentTarget {
        &self.target
    }

    #[must_use]
    pub const fn deadline_ms(&self) -> u64 {
        self.deadline_ms
    }

    #[must_use]
    pub fn idempotency_key(&self) -> &str {
        &self.idempotency_key
    }

    #[must_use]
    pub const fn params(&self) -> &BTreeMap<String, String> {
        &self.params
    }
}

/// One stream chunk of a chunked tool result.  `seq` ordering and gap
/// detection are enforced by the session layer; the schema freezes fields
/// and the payload bound only.
#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaapChunk {
    id: u64,
    seq: u32,
    data: String,
    is_final: bool,
}

impl CaapChunk {
    /// Creates a validated chunk.
    pub fn new(id: u64, seq: u32, data: &str, is_final: bool) -> Result<Self, CaapSchemaError> {
        if data.len() > MAX_CAAP_CHUNK_BYTES {
            return Err(CaapSchemaError::ChunkTooLarge);
        }
        Ok(Self {
            id,
            seq,
            data: data.to_owned(),
            is_final,
        })
    }

    /// Re-checks a decoded chunk against the size bound.
    pub fn validate(&self) -> Result<(), CaapSchemaError> {
        if self.data.len() > MAX_CAAP_CHUNK_BYTES {
            return Err(CaapSchemaError::ChunkTooLarge);
        }
        Ok(())
    }

    #[must_use]
    pub const fn id(&self) -> u64 {
        self.id
    }

    #[must_use]
    pub const fn seq(&self) -> u32 {
        self.seq
    }

    #[must_use]
    pub fn data(&self) -> &str {
        &self.data
    }

    #[must_use]
    pub const fn is_final(&self) -> bool {
        self.is_final
    }
}

/// Cancellation of an in-flight request.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaapCancel {
    id: u64,
}

impl CaapCancel {
    #[must_use]
    pub const fn new(id: u64) -> Self {
        Self { id }
    }

    #[must_use]
    pub const fn id(self) -> u64 {
        self.id
    }
}

/// Error reply terminating a request.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaapErrorReply {
    id: u64,
    error: CaapError,
}

impl CaapErrorReply {
    #[must_use]
    pub const fn new(id: u64, error: CaapError) -> Self {
        Self { id, error }
    }

    #[must_use]
    pub const fn id(self) -> u64 {
        self.id
    }

    #[must_use]
    pub const fn error(self) -> CaapError {
        self.error
    }
}
