//! Agent authorization grants (AGT-04, AG-003/AG-005 model).
//!
//! Every agent tool call must pass `GrantManager::authorize` with the
//! caller-supplied `(session, profile, capability, target)` quadruple.
//! The default is deny: any mismatch, expiry, revocation or unknown grant
//! is a stable rejection, and no API in this module widens an existing
//! grant.  Untrusted content (page text, model output, tool results) is
//! never an input to `authorize` — grants can only be issued through the
//! explicit `issue` path, whose callers must represent user confirmation
//! (the confirmation UI itself is AGT-05).
//!
//! Grants are in-process v1 state: no persistence, no clock, no IO; time
//! is injected as `now_ms` by the caller, mirroring the session module.

use crate::registry::is_token;
use crayon_domain::{AgentCapability, AgentTarget, CaapError, TabId};
use std::collections::HashMap;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum length of a client session token, in bytes (session module
/// uses the same registry charset).
const MAX_SESSION_TOKEN_LEN: usize = 64;

/// Maximum length of a profile scope token, in bytes.
const MAX_PROFILE_SCOPE_LEN: usize = 64;

/// Maximum length of a task identifier token, in bytes.
const MAX_TASK_ID_LEN: usize = 64;

/// Maximum number of live grants (bounded map rule).
pub const MAX_GRANTS: usize = 128;

/// Hard maximum lifetime of a grant, in milliseconds.
pub const MAX_GRANT_TTL_MS: u64 = 60 * 60 * 1000;

/// Maximum authorizations consumed by one task grant before it fails
/// closed (bounded use rule).
pub const MAX_TASK_GRANT_USES: u32 = 64;

/// Grant lifetime class (AG-003).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GrantKind {
    /// Consumed by the first successful authorize.
    SingleUse,
    /// Bound to one task id; bounded use count.
    Task,
    /// Bound to the client session; valid until revoked or expired.
    AppSession,
}

/// Grant failure.  Variants are stable and carry no user data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GrantError {
    /// No matching grant exists for the quadruple (default deny).
    Denied,
    /// The grant existed but expired.
    Expired,
    /// The grant was revoked (single, session-wide or profile-wide).
    Revoked,
    /// The bound target no longer matches.
    TargetStale,
    /// The grant store is at capacity; issue was rejected.
    CapacityExceeded,
    /// A caller-supplied token (session/profile/task) violates shape.
    InvalidToken,
    /// The requested TTL exceeds the hard bound.
    TtlExceeded,
    /// The task grant's use count is exhausted.
    UseLimitReached,
    /// The referenced grant id does not exist.
    UnknownGrant,
}

impl Display for GrantError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::Denied => "grant denied",
            Self::Expired => "grant expired",
            Self::Revoked => "grant revoked",
            Self::TargetStale => "grant target stale",
            Self::CapacityExceeded => "grant store at capacity",
            Self::InvalidToken => "grant token rejected",
            Self::TtlExceeded => "grant ttl exceeds bound",
            Self::UseLimitReached => "grant use limit reached",
            Self::UnknownGrant => "grant unknown",
        };
        formatter.write_str(message)
    }
}

impl Error for GrantError {}

impl GrantError {
    /// Stable mapping onto the closed CAAP error codes.
    #[must_use]
    pub const fn to_caap_error(self) -> CaapError {
        match self {
            Self::Denied | Self::Revoked | Self::UseLimitReached => CaapError::CapabilityDenied,
            Self::Expired => CaapError::DeadlineExceeded,
            Self::TargetStale => CaapError::TargetStale,
            Self::CapacityExceeded => CaapError::QueueFull,
            Self::InvalidToken | Self::TtlExceeded => CaapError::InvalidMessage,
            Self::UnknownGrant => CaapError::Unauthorized,
        }
    }
}

/// Validated profile scope token.  Grants never cross profile scopes.
#[derive(Clone, Debug, Eq, PartialEq, Hash)]
pub struct ProfileScope(String);

impl ProfileScope {
    /// Creates a validated profile scope token (registry charset).
    pub fn new(value: &str) -> Result<Self, GrantError> {
        if !is_token(value, MAX_PROFILE_SCOPE_LEN) {
            return Err(GrantError::InvalidToken);
        }
        Ok(Self(value.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Opaque grant identifier issued by [`GrantManager::issue`].
#[derive(Clone, Debug, Eq, PartialEq, Hash)]
pub struct GrantId(pub(crate) u64);

/// A single issued grant.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Grant {
    kind: GrantKind,
    session: String,
    profile: ProfileScope,
    capability: AgentCapability,
    target: Option<AgentTarget>,
    task: Option<String>,
    expires_at_ms: u64,
    uses: u32,
    revoked: bool,
}

impl Grant {
    #[must_use]
    pub const fn kind(&self) -> GrantKind {
        self.kind
    }

    #[must_use]
    pub fn session(&self) -> &str {
        &self.session
    }

    #[must_use]
    pub fn profile(&self) -> &ProfileScope {
        &self.profile
    }

    #[must_use]
    pub const fn capability(&self) -> AgentCapability {
        self.capability
    }

    #[must_use]
    pub const fn target(&self) -> Option<&AgentTarget> {
        self.target.as_ref()
    }

    #[must_use]
    pub fn task(&self) -> Option<&str> {
        self.task.as_deref()
    }

    #[must_use]
    pub const fn revoked(&self) -> bool {
        self.revoked
    }

    /// Whether the grant is bound to one specific target.  Confirmation
    /// UIs must render untargeted grants as "any target of this client
    /// and profile" so users cannot misread the scope (AGT-05).
    #[must_use]
    pub const fn is_targeted(&self) -> bool {
        self.target.is_some()
    }

    /// Closed-form scope descriptor for confirmation summaries; carries
    /// no page data and never fails.
    #[must_use]
    pub fn scope_summary(&self) -> String {
        match &self.target {
            None => format!("grant:{}:any-target", self.capability.wire_name()),
            Some(AgentTarget::Tab { tab }) => {
                format!("grant:{}:tab:{}", self.capability.wire_name(), tab)
            }
            Some(AgentTarget::ActiveTab) => {
                format!("grant:{}:active-tab", self.capability.wire_name())
            }
        }
    }

    #[must_use]
    pub const fn uses(&self) -> u32 {
        self.uses
    }
}

/// What a grant is issued for; `task` is required (and only valid) for
/// [`GrantKind::Task`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GrantRequest {
    pub kind: GrantKind,
    pub session: String,
    pub profile: ProfileScope,
    pub capability: AgentCapability,
    pub target: Option<AgentTarget>,
    pub task: Option<String>,
    pub ttl_ms: u64,
}

/// Outcome of a successful authorize: which grant authorized the call and
/// its remaining state.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Authorization {
    pub grant: GrantId,
    pub remaining_uses: Option<u32>,
}

/// Counters for diagnostics (bounded values only, no user data).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct GrantStats {
    pub live: usize,
    pub issued_total: u64,
    pub authorized_total: u64,
    pub denied_total: u64,
    pub revoked_total: u64,
}

/// In-process grant store with default-deny authorization.
pub struct GrantManager {
    grants: HashMap<GrantId, Grant>,
    next_id: u64,
    issued_total: u64,
    authorized_total: u64,
    denied_total: u64,
    revoked_total: u64,
}

impl Default for GrantManager {
    fn default() -> Self {
        Self::new()
    }
}

impl GrantManager {
    #[must_use]
    pub fn new() -> Self {
        Self {
            grants: HashMap::new(),
            next_id: 0,
            issued_total: 0,
            authorized_total: 0,
            denied_total: 0,
            revoked_total: 0,
        }
    }

    /// Issues a grant.  The caller must have collected explicit user
    /// confirmation for this exact `(session, profile, capability,
    /// target)` scope; nothing else in this crate can mint grants.
    pub fn issue(&mut self, request: GrantRequest, now_ms: u64) -> Result<GrantId, GrantError> {
        if !is_token(&request.session, MAX_SESSION_TOKEN_LEN) {
            return Err(GrantError::InvalidToken);
        }
        match (request.kind, request.task.as_deref()) {
            (GrantKind::Task, Some(task)) => {
                if !is_token(task, MAX_TASK_ID_LEN) {
                    return Err(GrantError::InvalidToken);
                }
            }
            (GrantKind::Task, None) => return Err(GrantError::InvalidToken),
            (_, Some(_)) => return Err(GrantError::InvalidToken),
            _ => {}
        }
        if request.ttl_ms == 0 || request.ttl_ms > MAX_GRANT_TTL_MS {
            return Err(GrantError::TtlExceeded);
        }
        if self.grants.len() >= MAX_GRANTS {
            return Err(GrantError::CapacityExceeded);
        }
        let id = GrantId(self.next_id);
        self.next_id = self
            .next_id
            .checked_add(1)
            .ok_or(GrantError::CapacityExceeded)?;
        self.grants.insert(
            id.clone(),
            Grant {
                kind: request.kind,
                session: request.session,
                profile: request.profile,
                capability: request.capability,
                target: request.target,
                task: request.task,
                expires_at_ms: now_ms.saturating_add(request.ttl_ms),
                uses: 0,
                revoked: false,
            },
        );
        self.issued_total = self.issued_total.saturating_add(1);
        Ok(id)
    }

    /// Default-deny authorization check over the caller-supplied
    /// quadruple.  On success the grant's use accounting advances
    /// (single-use grants are consumed and removed; task grants count
    /// toward their bounded use limit).  No page-, model- or tool-derived
    /// value participates in this decision.
    pub fn authorize(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        capability: AgentCapability,
        target: Option<&AgentTarget>,
        now_ms: u64,
    ) -> Result<Authorization, GrantError> {
        // Prefer a non-revoked match; only report `Revoked` when every
        // matching grant is revoked.
        let mut matched: Option<GrantId> = None;
        let mut matched_revoked = false;
        for (id, grant) in &self.grants {
            if grant.session == session
                && grant.profile == *profile
                && grant.capability == capability
                && targets_match(grant.target.as_ref(), target)
            {
                if grant.revoked {
                    matched_revoked = true;
                } else {
                    matched = Some(id.clone());
                    break;
                }
            }
        }
        let Some(id) = matched else {
            self.denied_total = self.denied_total.saturating_add(1);
            return Err(if matched_revoked {
                GrantError::Revoked
            } else {
                GrantError::Denied
            });
        };
        let Some(grant) = self.grants.get_mut(&id) else {
            self.denied_total = self.denied_total.saturating_add(1);
            return Err(GrantError::Denied);
        };
        if now_ms >= grant.expires_at_ms {
            self.grants.remove(&id);
            self.denied_total = self.denied_total.saturating_add(1);
            return Err(GrantError::Expired);
        }
        if grant.kind == GrantKind::Task && grant.uses >= MAX_TASK_GRANT_USES {
            self.denied_total = self.denied_total.saturating_add(1);
            return Err(GrantError::UseLimitReached);
        }
        grant.uses = grant.uses.saturating_add(1);
        let remaining_uses = match grant.kind {
            GrantKind::SingleUse => None,
            GrantKind::Task => Some(MAX_TASK_GRANT_USES - grant.uses),
            GrantKind::AppSession => None,
        };
        if grant.kind == GrantKind::SingleUse {
            self.grants.remove(&id);
        }
        self.authorized_total = self.authorized_total.saturating_add(1);
        Ok(Authorization {
            grant: id,
            remaining_uses,
        })
    }

    /// Revokes one grant by id; revoking an unknown or already-revoked
    /// grant is a stable `UnknownGrant`/idempotent rejection without
    /// side effects on other grants.
    pub fn revoke(&mut self, id: &GrantId) -> Result<(), GrantError> {
        match self.grants.get_mut(id) {
            Some(grant) if grant.revoked => Err(GrantError::Revoked),
            Some(grant) => {
                grant.revoked = true;
                self.revoked_total = self.revoked_total.saturating_add(1);
                Ok(())
            }
            None => Err(GrantError::UnknownGrant),
        }
    }

    /// Revokes every grant of one client session (immediate, AG-003).
    pub fn revoke_session(&mut self, session: &str) -> usize {
        let mut revoked = 0;
        for grant in self.grants.values_mut() {
            if grant.session == session && !grant.revoked {
                grant.revoked = true;
                revoked += 1;
            }
        }
        self.revoked_total = self.revoked_total.saturating_add(revoked as u64);
        revoked
    }

    /// Revokes every grant in one profile scope (immediate, AG-003).
    pub fn revoke_profile(&mut self, profile: &ProfileScope) -> usize {
        let mut revoked = 0;
        for grant in self.grants.values_mut() {
            if grant.profile == *profile && !grant.revoked {
                grant.revoked = true;
                revoked += 1;
            }
        }
        self.revoked_total = self.revoked_total.saturating_add(revoked as u64);
        revoked
    }

    /// Invalidates every grant bound to `tab` (target changed: navigation,
    /// generation advance).  Untargeted grants stay untouched.
    pub fn invalidate_target(&mut self, tab: &TabId) -> usize {
        let mut invalidated = 0;
        for grant in self.grants.values_mut() {
            if matches!(&grant.target, Some(AgentTarget::Tab { tab: bound }) if *bound == *tab)
                && !grant.revoked
            {
                grant.revoked = true;
                invalidated += 1;
            }
        }
        self.revoked_total = self.revoked_total.saturating_add(invalidated as u64);
        invalidated
    }

    /// Removes expired grants; returns how many were dropped.
    pub fn sweep_expired(&mut self, now_ms: u64) -> usize {
        let before = self.grants.len();
        self.grants
            .retain(|_, grant| !(grant.revoked || now_ms >= grant.expires_at_ms));
        before - self.grants.len()
    }

    /// Reads back one grant (diagnostics/preview only).
    pub fn get(&self, id: &GrantId) -> Option<&Grant> {
        self.grants.get(id)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.grants.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.grants.is_empty()
    }

    /// Bounded diagnostic counters.
    #[must_use]
    pub fn stats(&self) -> GrantStats {
        GrantStats {
            live: self.grants.len(),
            issued_total: self.issued_total,
            authorized_total: self.authorized_total,
            denied_total: self.denied_total,
            revoked_total: self.revoked_total,
        }
    }
}

/// Target matching: an untargeted grant authorizes any requested target;
/// a targeted grant authorizes only its exact target.
fn targets_match(grant_target: Option<&AgentTarget>, requested: Option<&AgentTarget>) -> bool {
    match grant_target {
        None => true,
        Some(bound) => Some(bound) == requested,
    }
}

#[cfg(test)]
#[path = "grant_tests.rs"]
mod tests;
