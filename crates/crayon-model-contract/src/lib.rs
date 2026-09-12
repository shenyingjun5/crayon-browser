//! Model provider contract (CNT-11 ADR).
//!
//! Closed types for the BYOK model provider integration. The product
//! never proxies, caches or retains model traffic: requests are
//! user-initiated, sent to user-configured endpoints, and responses are
//! consumed in-session. The feature is default-OFF.

/// Maximum prompt/markdown bytes per request.
pub const MAX_MODEL_REQUEST_BYTES: usize = 256 * 1024;
/// Maximum response bytes.
pub const MAX_MODEL_RESPONSE_BYTES: usize = 1024 * 1024;
/// Default timeout in milliseconds.
pub const DEFAULT_MODEL_TIMEOUT_MS: u64 = 30_000;

/// Provider configuration. The endpoint is user-supplied; the API key is
/// stored separately in `SecureStore` and referenced by key handle.
#[derive(Clone, Debug, Eq, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ModelProviderConfig {
    /// User-chosen provider name (`openai-compat`, `ollama`, etc.).
    pub provider_name: String,
    /// User-configured endpoint URL (validated https in production).
    pub endpoint_url: String,
    /// Model identifier (e.g. `gpt-4o`, `llama3`).
    pub model_name: String,
    /// SecureStore key handle for the API key (never the key itself).
    pub api_key_handle: String,
    pub timeout_ms: u64,
    pub max_tokens: u32,
}

impl ModelProviderConfig {
    pub fn validate(&self) -> Result<(), ModelContractError> {
        if self.provider_name.is_empty() || self.provider_name.len() > 64 {
            return Err(ModelContractError::InvalidProviderName);
        }
        if !self.endpoint_url.starts_with("https://") {
            return Err(ModelContractError::InsecureEndpoint);
        }
        if self.model_name.is_empty() || self.model_name.len() > 128 {
            return Err(ModelContractError::InvalidModelName);
        }
        if self.timeout_ms == 0 || self.timeout_ms > 300_000 {
            return Err(ModelContractError::InvalidTimeout);
        }
        if self.max_tokens == 0 || self.max_tokens > 128_000 {
            return Err(ModelContractError::InvalidMaxTokens);
        }
        Ok(())
    }
}

/// Contract validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelContractError {
    InvalidProviderName,
    InsecureEndpoint,
    InvalidModelName,
    InvalidTimeout,
    InvalidMaxTokens,
    PayloadTooLarge,
    ResponseTooLarge,
}

/// One user-initiated model request.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelRequest {
    /// Bounded markdown/prompt text.
    pub prompt: String,
    pub markdown: String,
    pub max_tokens: u32,
}

/// One model response.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ModelResponse {
    pub text: String,
    pub prompt_tokens: u64,
    pub completion_tokens: u64,
}

/// Provider execution port (product assembly implements the HTTP layer).
pub trait ModelProviderPort: Send {
    fn execute(
        &mut self,
        config: &ModelProviderConfig,
        request: &ModelRequest,
    ) -> Result<ModelResponse, ModelContractError>;
}
