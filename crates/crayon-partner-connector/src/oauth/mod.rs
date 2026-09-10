//! OAuth authorization helpers and the provider/tenant token vault
//! (HUB-11).
//!
//! Tokens live only inside the injected platform `SecureStore`, keyed by
//! `(connector, account)` so tenants cannot read each other's material.
//! The cryptographic primitives (randomness, SHA-256) are injected by the
//! host — this crate implements policy, not crypto.

use std::collections::BTreeMap;

use crayon_platform_api::secure_store::validate_key;
use crayon_platform_api::secure_store::{SecureStore, SecureStoreError};

use crate::api::{ConnectorDescriptor, ConnectorId, TokenVaultPort};

const TOKEN_KEY_PREFIX: &str = "pc-";
/// OAuth state: 32 bytes of entropy rendered as 64 hex characters.
pub const OAUTH_STATE_HEX_LEN: usize = 64;
/// RFC 7636 code verifier bounds and charset.
pub const PKCE_VERIFIER_MIN_LEN: usize = 43;
pub const PKCE_VERIFIER_MAX_LEN: usize = 128;

/// Failure of vault/oauth operations; variants never carry input.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum OAuthError {
    /// Key or value rejected by the platform store.
    StoreRejected,
    /// The store backend failed.
    Backend(SecureStoreError),
    /// The state/verifier/challenge input violates its closed grammar.
    InvalidInput,
    /// The redirect target is not an exactly registered entry.
    RedirectUnknown,
    /// The requested scope set exceeds or escapes the descriptor.
    ScopeEscalation,
}

impl std::fmt::Display for OAuthError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::StoreRejected => formatter.write_str("vault key rejected"),
            Self::Backend(_) => formatter.write_str("token vault backend failed"),
            Self::InvalidInput => formatter.write_str("oauth input rejected"),
            Self::RedirectUnknown => formatter.write_str("redirect target is not registered"),
            Self::ScopeEscalation => formatter.write_str("requested scope exceeds the descriptor"),
        }
    }
}

impl std::error::Error for OAuthError {}

impl From<SecureStoreError> for OAuthError {
    fn from(error: SecureStoreError) -> Self {
        Self::Backend(error)
    }
}

/// Token vault over the platform store. The injected `SecureStore`
/// instance enforces OS-user and Profile isolation; keys are namespaced by
/// `(connector key, account)` so tenants cannot cross-read.
pub struct TokenVault<S: SecureStore> {
    store: S,
    handles: BTreeMap<String, u64>,
    next_handle: u64,
}

impl<S: SecureStore> TokenVault<S> {
    #[must_use]
    pub fn new(store: S) -> Self {
        Self {
            store,
            handles: BTreeMap::new(),
            next_handle: 0,
        }
    }

    fn key(connector: &ConnectorId, account: &str) -> Result<String, OAuthError> {
        let raw = format!("{TOKEN_KEY_PREFIX}{}.{}", connector.key(), account);
        if account.is_empty() || account.len() > 64 {
            return Err(OAuthError::InvalidInput);
        }
        validate_key(&raw).map_err(|_| OAuthError::StoreRejected)?;
        Ok(raw)
    }

    /// Stores or replaces the token for `(connector, account)`.
    pub fn store_token(
        &mut self,
        connector: &ConnectorId,
        account: &str,
        token: &[u8],
    ) -> Result<(), OAuthError> {
        let key = Self::key(connector, account)?;
        self.store.store(&key, token)?;
        self.handles.entry(key).or_insert_with(|| {
            self.next_handle += 1;
            self.next_handle
        });
        Ok(())
    }

    /// The stable opaque handle for `(connector, account)`; `None` when no
    /// token exists. Handles never expose token bytes.
    #[must_use]
    pub fn token_handle(&self, connector: &ConnectorId, account: &str) -> Option<u64> {
        let key = Self::key(connector, account).ok()?;
        self.handles.get(&key).copied()
    }

    /// Loads the token bytes for the HUB-12 network port. Crate-internal
    /// only — the public `TokenVaultPort` surface never returns tokens.
    #[allow(dead_code)] // consumed by HUB-12 network port assembly
    pub(crate) fn load_token(
        &self,
        connector: &ConnectorId,
        account: &str,
    ) -> Result<Option<Vec<u8>>, OAuthError> {
        let key = Self::key(connector, account)?;
        Ok(self.store.load(&key)?)
    }

    /// Clears one account's token. Idempotent.
    pub fn clear_token(
        &mut self,
        connector: &ConnectorId,
        account: &str,
    ) -> Result<(), OAuthError> {
        let key = Self::key(connector, account)?;
        self.store.delete(&key)?;
        self.handles.remove(&key);
        Ok(())
    }

    /// Clears every token of one connector (all accounts). Returns the
    /// number of accounts cleared.
    pub fn clear_connector(&mut self, connector: &ConnectorId) -> Result<usize, OAuthError> {
        let prefix = format!("{TOKEN_KEY_PREFIX}{}.", connector.key());
        let keys: Vec<String> = self
            .handles
            .keys()
            .filter(|key| key.starts_with(&prefix))
            .cloned()
            .collect();
        let mut cleared = 0usize;
        for key in &keys {
            self.store.delete(key)?;
            self.handles.remove(key);
            cleared += 1;
        }
        Ok(cleared)
    }
}

impl<S: SecureStore> TokenVaultPort for TokenVault<S> {
    fn store(&mut self, connector: &ConnectorId, account: &str, token: &[u8]) {
        let _ = self.store_token(connector, account, token);
    }

    fn token_handle(&self, connector: &ConnectorId, account: &str) -> Option<u64> {
        self.token_handle(connector, account)
    }
}

/// CSRF state token for the authorization redirect round-trip.
pub struct OAuthState;

impl OAuthState {
    /// Renders 32 bytes of host-injected entropy as hex.
    #[must_use]
    pub fn generate(mut entropy: impl FnMut() -> u8) -> String {
        let mut hex = String::with_capacity(OAUTH_STATE_HEX_LEN);
        for _ in 0..(OAUTH_STATE_HEX_LEN / 2) {
            hex.push_str(&format!("{:02x}", entropy()));
        }
        hex
    }

    /// Constant-time comparison of the expected and received state.
    #[must_use]
    pub fn verify(expected: &str, received: &str) -> bool {
        if expected.len() != received.len() {
            return false;
        }
        let mut diff = 0u8;
        for (a, b) in expected.bytes().zip(received.bytes()) {
            diff |= a ^ b;
        }
        diff == 0
    }
}

/// SHA-256 injection point: the host supplies the digest implementation.
pub trait Sha256Port {
    fn sha256(&mut self, data: &[u8]) -> [u8; 32];
}

/// PKCE (RFC 7636) S256 challenge helpers.
pub struct PkceChallenge;

impl PkceChallenge {
    /// Validates a code verifier (43..=128 chars of
    /// `[A-Za-z0-9\-._~]`).
    #[must_use]
    pub fn valid_verifier(verifier: &str) -> bool {
        (PKCE_VERIFIER_MIN_LEN..=PKCE_VERIFIER_MAX_LEN).contains(&verifier.len())
            && verifier
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || matches!(b, b'-' | b'.' | b'_' | b'~'))
    }

    /// Computes the `S256` challenge for a valid verifier.
    pub fn s256(verifier: &str, sha: &mut dyn Sha256Port) -> Result<String, OAuthError> {
        if !Self::valid_verifier(verifier) {
            return Err(OAuthError::InvalidInput);
        }
        let digest = sha.sha256(verifier.as_bytes());
        Ok(base64url_no_pad(&digest))
    }

    /// Verifies a client-presented challenge against a verifier.
    pub fn verify(
        verifier: &str,
        challenge: &str,
        sha: &mut dyn Sha256Port,
    ) -> Result<bool, OAuthError> {
        Ok(Self::s256(verifier, sha)? == challenge)
    }
}

/// URL-safe base64 without padding (RFC 4648 §5).
#[must_use]
pub fn base64url_no_pad(data: &[u8]) -> String {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    let mut out = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = *chunk.get(1).unwrap_or(&0) as u32;
        let b2 = *chunk.get(2).unwrap_or(&0) as u32;
        let triple = (b0 << 16) | (b1 << 8) | b2;
        out.push(ALPHABET[(triple >> 18) as usize & 0x3f] as char);
        out.push(ALPHABET[(triple >> 12) as usize & 0x3f] as char);
        if chunk.len() > 1 {
            out.push(ALPHABET[(triple >> 6) as usize & 0x3f] as char);
        }
        if chunk.len() > 2 {
            out.push(ALPHABET[triple as usize & 0x3f] as char);
        }
    }
    out
}

/// Redirect-target policy: only exact registered entries are allowed —
/// no prefix rules, no open-redirect surface.
pub fn validate_redirect(requested: &str, allowed: &[&str]) -> Result<(), OAuthError> {
    if !requested.is_empty() && allowed.contains(&requested) {
        return Ok(());
    }
    Err(OAuthError::RedirectUnknown)
}

/// Minimal-scope check: every requested scope must exist in the verified
/// descriptor's scope set.
pub fn validate_scopes(
    requested: &[&str],
    descriptor: &ConnectorDescriptor,
) -> Result<(), OAuthError> {
    if requested.is_empty() {
        return Err(OAuthError::ScopeEscalation);
    }
    let owned = descriptor.scopes();
    for scope in requested {
        if !owned.iter().any(|allowed| allowed == scope) {
            return Err(OAuthError::ScopeEscalation);
        }
    }
    Ok(())
}

#[cfg(test)]
mod oauth_tests;
