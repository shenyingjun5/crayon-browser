//! Short-lived encrypted checkpoint storage (WFL-04).
//!
//! Encryption and OS-user/Profile isolation are owned by the injected platform
//! `SecureStore` (DPAPI on Windows, Keychain on macOS). This layer never writes
//! files and accepts no arbitrary checkpoint payload.

use crayon_domain::{Checkpoint, CheckpointState, WORKFLOW_SCHEMA_VERSION};
use crayon_platform_api::secure_store::{validate_key, SecureStore, SecureStoreError, MAX_KEY_LEN};

const KEY_PREFIX: &str = "wflcp-";
const MAX_CHECKPOINT_ID_BYTES: usize = MAX_KEY_LEN - KEY_PREFIX.len();

/// Closed checkpoint storage failure; no key or stored bytes are exposed.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CheckpointStoreError {
    InvalidId,
    InvalidCheckpoint,
    NotFound,
    Expired,
    Backend(SecureStoreError),
}

impl std::fmt::Display for CheckpointStoreError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidId => formatter.write_str("checkpoint id rejected"),
            Self::InvalidCheckpoint => formatter.write_str("checkpoint record rejected"),
            Self::NotFound => formatter.write_str("checkpoint not found"),
            Self::Expired => formatter.write_str("checkpoint expired"),
            Self::Backend(error) => error.fmt(formatter),
        }
    }
}

impl std::error::Error for CheckpointStoreError {}

/// Profile-scoped store. The caller must inject the SecureStore instance for
/// the current OS user and Profile; instances must never be shared by Profiles.
pub struct CheckpointStore<S: SecureStore> {
    backend: S,
}

impl<S: SecureStore> CheckpointStore<S> {
    #[must_use]
    pub const fn new(backend: S) -> Self {
        Self { backend }
    }

    /// Saves a live minimal checkpoint. Arbitrary payload is rejected so page
    /// content, field values and secrets cannot cross this layer.
    pub fn save(
        &mut self,
        id: &str,
        checkpoint: &Checkpoint,
        now_ms: u64,
    ) -> Result<(), CheckpointStoreError> {
        let key = checkpoint_key(id)?;
        validate_checkpoint(checkpoint, now_ms)?;
        let encoded =
            serde_json::to_vec(checkpoint).map_err(|_| CheckpointStoreError::InvalidCheckpoint)?;
        self.backend
            .store(&key, &encoded)
            .map_err(CheckpointStoreError::Backend)
    }

    /// Loads and consumes once. The encrypted record is deleted before the
    /// checkpoint is returned; a delete failure returns no checkpoint.
    pub fn take(&mut self, id: &str, now_ms: u64) -> Result<Checkpoint, CheckpointStoreError> {
        let key = checkpoint_key(id)?;
        let encoded = self
            .backend
            .load(&key)
            .map_err(CheckpointStoreError::Backend)?
            .ok_or(CheckpointStoreError::NotFound)?;
        let mut checkpoint: Checkpoint = match serde_json::from_slice(&encoded) {
            Ok(checkpoint) => checkpoint,
            Err(_) => return self.clear_invalid(&key, CheckpointStoreError::InvalidCheckpoint),
        };
        if checkpoint.expired_at(now_ms) {
            return self.clear_invalid(&key, CheckpointStoreError::Expired);
        }
        if validate_checkpoint(&checkpoint, now_ms).is_err() {
            return self.clear_invalid(&key, CheckpointStoreError::InvalidCheckpoint);
        }
        checkpoint
            .consume(now_ms)
            .map_err(|_| CheckpointStoreError::InvalidCheckpoint)?;
        self.backend
            .delete(&key)
            .map_err(CheckpointStoreError::Backend)?;
        Ok(checkpoint)
    }

    /// Idempotently clears one checkpoint.
    pub fn clear(&mut self, id: &str) -> Result<(), CheckpointStoreError> {
        let key = checkpoint_key(id)?;
        self.backend
            .delete(&key)
            .map_err(CheckpointStoreError::Backend)
    }

    fn clear_invalid<T>(
        &mut self,
        key: &str,
        reason: CheckpointStoreError,
    ) -> Result<T, CheckpointStoreError> {
        self.backend
            .delete(key)
            .map_err(CheckpointStoreError::Backend)?;
        Err(reason)
    }

    /// Returns the backend for controlled shutdown/testing ownership transfer.
    #[must_use]
    pub fn into_inner(self) -> S {
        self.backend
    }
}

fn checkpoint_key(id: &str) -> Result<String, CheckpointStoreError> {
    if id.is_empty()
        || id.len() > MAX_CHECKPOINT_ID_BYTES
        || !id
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'-')
    {
        return Err(CheckpointStoreError::InvalidId);
    }
    let key = format!("{KEY_PREFIX}{id}");
    validate_key(&key).map_err(|_| CheckpointStoreError::InvalidId)?;
    Ok(key)
}

fn validate_checkpoint(checkpoint: &Checkpoint, now_ms: u64) -> Result<(), CheckpointStoreError> {
    if checkpoint.schema_version != WORKFLOW_SCHEMA_VERSION
        || checkpoint.state != CheckpointState::Live
        || !checkpoint.payload.is_empty()
        || checkpoint.expired_at(now_ms)
    {
        return Err(CheckpointStoreError::InvalidCheckpoint);
    }
    Ok(())
}
