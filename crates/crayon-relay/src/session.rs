//! Session model: CSPRNG tokens, receiver binding, TTL and revocation
//! triggers (MED-09; design §11.2; RL-002..RL-005).
//!
//! Hard rules:
//! - session tokens and resource ids are 128-bit CSPRNG values and never
//!   contain the upstream URL (RL-002);
//! - authorization is checked before any upstream access (RL-003);
//! - stop/revoke is immediate and idempotent, and every lifecycle trigger
//!   (navigation, route lost, device replaced, profile destroyed, app exit)
//!   revokes sessions and their secrets (RL-004/RL-005);
//! - time is caller-supplied logical milliseconds (no wall clock here).

use crayon_domain::{DeviceId, ResourceId, SessionGeneration, SessionId};
use crayon_ipc_schema::SessionSecret;
use std::fmt::{Debug, Formatter};
use std::net::IpAddr;

/// Maximum live sessions (bounded collection rule).
pub const MAX_SESSIONS: usize = 32;
/// Maximum resources per session.
pub const MAX_RESOURCES_PER_SESSION: usize = 128;
/// Default session TTL: 2 hours (design §11.2).
pub const DEFAULT_SESSION_TTL_MS: u64 = 2 * 3600 * 1000;

/// 128-bit session token from the system CSPRNG (RL-002). The hex form is
/// the URL path segment; the type is never serialized into DTOs or logs.
#[derive(Clone, Eq, PartialEq)]
pub struct SessionToken([u8; 16]);

impl SessionToken {
    /// Generates a fresh token from the system CSPRNG.
    #[must_use]
    pub fn generate() -> Self {
        let mut bytes = [0u8; 16];
        getrandom::getrandom(&mut bytes).expect("system CSPRNG unavailable");
        Self(bytes)
    }

    /// Lowercase hex form for route paths (32 chars, 128-bit entropy).
    #[must_use]
    pub fn as_hex(&self) -> String {
        self.0.iter().map(|b| format!("{b:02x}")).collect()
    }

    /// Parses a hex path segment back into a token (routing only).
    #[must_use]
    pub fn from_hex(text: &str) -> Option<Self> {
        if text.len() != 32 {
            return None;
        }
        let mut bytes = [0u8; 16];
        for (i, chunk) in text.as_bytes().chunks(2).enumerate() {
            let hex = std::str::from_utf8(chunk).ok()?;
            bytes[i] = u8::from_str_radix(hex, 16).ok()?;
        }
        Some(Self(bytes))
    }

    /// Constant-time equality (tokens are bearer secrets on the LAN route).
    pub(crate) fn ct_eq(&self, other: &Self) -> bool {
        self.0
            .iter()
            .zip(other.0.iter())
            .fold(0u8, |acc, (a, b)| acc | (a ^ b))
            == 0
    }
}

impl Debug for SessionToken {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.write_str("SessionToken(REDACTED)")
    }
}

/// Upstream resource registered inside a session (opaque id → host binding).
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SessionResource {
    pub id: ResourceId,
    /// Upstream host this resource resolves to (validated against the
    /// session allow-set at registration).
    pub upstream_host: String,
    /// Nesting depth (master playlist = 0; children increment).
    pub depth: u8,
}

/// Generates an opaque resource id (CSPRNG hex; RL-002).
#[must_use]
pub fn generate_resource_id() -> ResourceId {
    let mut bytes = [0u8; 16];
    getrandom::getrandom(&mut bytes).expect("system CSPRNG unavailable");
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    ResourceId::new(&hex).expect("hex is a valid resource id charset")
}

/// Authorization failure (RL-003): mapped to 401/403 by the router; the
/// caller must not touch upstream on any error.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SessionAuthError {
    /// Unknown or revoked token → 401.
    UnknownSession,
    /// Token known but receiver binding (device/IP) mismatched → 403.
    ReceiverMismatch,
    /// Session TTL exceeded → 401.
    SessionExpired,
    /// Resource id not registered in this session → 404 class handled by router.
    UnknownResource,
}

/// Revocation triggers (RL-005).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RevokeReason {
    Navigation,
    RouteLost,
    DeviceReplaced,
    ProfileDestroyed,
    AppExit,
    Stopped,
}

struct SessionRecord {
    id: SessionId,
    token: SessionToken,
    #[allow(dead_code)] // zeroized on drop; recipe material arrives with the vault (MED-10)
    secret: SessionSecret,
    receiver: DeviceId,
    receiver_ip: Option<IpAddr>,
    upstream_allow_set: Vec<String>,
    resources: Vec<SessionResource>,
    generation: SessionGeneration,
    created_ms: u64,
    ttl_ms: u64,
}

impl SessionRecord {
    fn is_expired(&self, now_ms: u64) -> bool {
        now_ms > self.created_ms.saturating_add(self.ttl_ms)
    }

    fn authorize(&self, receiver_ip: Option<IpAddr>, now_ms: u64) -> Result<(), SessionAuthError> {
        if self.is_expired(now_ms) {
            return Err(SessionAuthError::SessionExpired);
        }
        if let Some(bound) = self.receiver_ip {
            if receiver_ip != Some(bound) {
                return Err(SessionAuthError::ReceiverMismatch);
            }
        }
        Ok(())
    }
}

/// Session registry: creation, authorization, TTL and revocation.
pub struct SessionRegistry {
    sessions: Vec<SessionRecord>,
    next_serial: u64,
}

/// Granted access to one registered resource.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SessionAccess {
    pub session_id: SessionId,
    pub generation: SessionGeneration,
    pub resource: SessionResource,
    /// Fixed upstream allow-set of the session (cloned for the fetch path).
    pub upstream_allow_set: Vec<String>,
}

impl SessionRegistry {
    #[must_use]
    pub fn new() -> Self {
        Self {
            sessions: Vec::new(),
            next_serial: 0,
        }
    }

    /// Creates a session for a paired receiver. `upstream_allow_set` fixes
    /// the only upstream hosts this session may ever reach (host names,
    /// compared case-insensitively).
    pub fn create_session(
        &mut self,
        receiver: DeviceId,
        receiver_ip: Option<IpAddr>,
        upstream_allow_set: Vec<String>,
        ttl_ms: u64,
        now_ms: u64,
    ) -> Option<SessionGrantView> {
        if self.sessions.len() >= MAX_SESSIONS {
            return None;
        }
        self.next_serial += 1;
        let id = SessionId::new(&format!("sess-{:016x}", self.next_serial)).ok()?;
        let token = SessionToken::generate();
        let grant = SessionGrantView {
            session_id: id.clone(),
            token_hex: token.as_hex(),
            generation: SessionGeneration::INITIAL,
        };
        let mut secret_bytes = [0u8; 32];
        getrandom::getrandom(&mut secret_bytes).expect("system CSPRNG unavailable");
        self.sessions.push(SessionRecord {
            id,
            token,
            secret: SessionSecret::from_bytes(secret_bytes),
            receiver,
            receiver_ip,
            upstream_allow_set: upstream_allow_set
                .into_iter()
                .map(|h| h.to_ascii_lowercase())
                .collect(),
            resources: Vec::new(),
            generation: SessionGeneration::INITIAL,
            created_ms: now_ms,
            ttl_ms,
        });
        Some(grant)
    }

    /// Registers a resource inside a session; the upstream host must be in
    /// the session allow-set (no runtime expansion, RL-006 precondition).
    pub fn register_resource(
        &mut self,
        token_hex: &str,
        resource_id: ResourceId,
        upstream_host: &str,
        depth: u8,
    ) -> Result<(), SessionAuthError> {
        let session = self
            .find_mut(token_hex)
            .ok_or(SessionAuthError::UnknownSession)?;
        let host = upstream_host.to_ascii_lowercase();
        if !session.upstream_allow_set.iter().any(|h| h == &host) {
            return Err(SessionAuthError::ReceiverMismatch);
        }
        if session.resources.len() >= MAX_RESOURCES_PER_SESSION {
            return Err(SessionAuthError::UnknownResource);
        }
        if session.resources.iter().any(|r| r.id == resource_id) {
            return Ok(()); // idempotent re-registration
        }
        session.resources.push(SessionResource {
            id: resource_id,
            upstream_host: host,
            depth,
        });
        Ok(())
    }

    /// Authorizes a media request and returns the registered resource.
    /// Checked before any upstream access (RL-003).
    pub fn authorize(
        &self,
        token_hex: &str,
        resource_id: &ResourceId,
        receiver_ip: Option<IpAddr>,
        now_ms: u64,
    ) -> Result<SessionAccess, SessionAuthError> {
        let session = self
            .find(token_hex)
            .ok_or(SessionAuthError::UnknownSession)?;
        session.authorize(receiver_ip, now_ms)?;
        let resource = session
            .resources
            .iter()
            .find(|r| r.id == *resource_id)
            .ok_or(SessionAuthError::UnknownResource)?;
        Ok(SessionAccess {
            session_id: session.id.clone(),
            generation: session.generation,
            resource: resource.clone(),
            upstream_allow_set: session.upstream_allow_set.clone(),
        })
    }

    /// Stops one session (RL-004): the token dies immediately and the record
    /// (including its secret) is dropped. Idempotent.
    pub fn stop(&mut self, token_hex: &str) -> bool {
        let before = self.sessions.len();
        if let Some(token) = SessionToken::from_hex(token_hex) {
            self.sessions.retain(|s| !s.token.ct_eq(&token));
        }
        self.sessions.len() != before
    }

    /// Revokes sessions for a lifecycle trigger (RL-005). Navigation,
    /// profile destruction and app exit revoke everything; route lost and
    /// device replacement revoke the sessions bound to that receiver.
    /// Returns the revoked session ids so the vault can purge their recipes.
    pub fn revoke(&mut self, reason: RevokeReason, receiver: Option<&DeviceId>) -> Vec<SessionId> {
        let removed: Vec<SessionId> = match reason {
            RevokeReason::Navigation | RevokeReason::ProfileDestroyed | RevokeReason::AppExit => {
                self.sessions.iter().map(|s| s.id.clone()).collect()
            }
            RevokeReason::RouteLost | RevokeReason::DeviceReplaced | RevokeReason::Stopped => {
                match receiver {
                    Some(device) => self
                        .sessions
                        .iter()
                        .filter(|s| &s.receiver == device)
                        .map(|s| s.id.clone())
                        .collect(),
                    None => Vec::new(),
                }
            }
        };
        match reason {
            RevokeReason::Navigation | RevokeReason::ProfileDestroyed | RevokeReason::AppExit => {
                self.sessions.clear()
            }
            RevokeReason::RouteLost | RevokeReason::DeviceReplaced | RevokeReason::Stopped => {
                if let Some(device) = receiver {
                    self.sessions.retain(|s| &s.receiver != device);
                }
            }
        }
        removed
    }

    /// Drops expired sessions relative to `now_ms`; returns the count.
    pub fn expire(&mut self, now_ms: u64) -> usize {
        let before = self.sessions.len();
        self.sessions.retain(|s| !s.is_expired(now_ms));
        before - self.sessions.len()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.sessions.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.sessions.is_empty()
    }

    fn find(&self, token_hex: &str) -> Option<&SessionRecord> {
        let token = SessionToken::from_hex(token_hex)?;
        self.sessions.iter().find(|s| s.token.ct_eq(&token))
    }

    fn find_mut(&mut self, token_hex: &str) -> Option<&mut SessionRecord> {
        let token = SessionToken::from_hex(token_hex)?;
        self.sessions.iter_mut().find(|s| s.token.ct_eq(&token))
    }
}

impl Default for SessionRegistry {
    fn default() -> Self {
        Self::new()
    }
}

/// Creation result: everything the router needs to build media URLs.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SessionGrantView {
    pub session_id: SessionId,
    pub token_hex: String,
    pub generation: SessionGeneration,
}
