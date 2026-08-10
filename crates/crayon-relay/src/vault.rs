//! Recipe vault (MED-10): upstream URLs and scoped headers live here and
//! nowhere else.
//!
//! Contract:
//! - no serialization, no `Clone`, redacting `Debug` (RL-014);
//! - secrets/URLs are `Zeroizing` and die with the vault entry (RL-004/005);
//! - header scope is type-level: a recipe can carry Referer/User-Agent only
//!   — Cookie/Authorization cannot be expressed (RL-008/RL-015);
//! - every redirect hop re-checks the origin scope; cross-origin hops strip
//!   scoped headers (RL-015).

use crayon_domain::{ResourceId, SessionId};
use std::collections::HashMap;
use std::fmt::{Debug, Formatter};
use zeroize::Zeroizing;

/// Maximum recipes per session (aligned with the session registry).
const MAX_RECIPES_PER_SESSION: usize = 128;

/// Header scope decision for one redirect hop (RL-015).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HopScope {
    /// Same origin: scoped headers may be attached.
    CarryHeaders,
    /// Cross-origin: scoped headers must be stripped.
    StripHeaders,
}

/// Scope validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScopeError {
    UnsupportedScheme,
    /// Hop/resolve target escapes the recipe origin.
    OriginEscape,
    UnknownRecipe,
    CapacityExceeded,
}

/// One upstream recipe. Not `Clone`, not `Serialize`; `Debug` shows only the
/// redacted origin and path prefix.
pub struct UpstreamRecipe {
    /// Full upstream URL including any signature query — zeroized on drop.
    url: Zeroizing<String>,
    /// `scheme://host[:port]` — the scope boundary.
    origin: String,
    /// Directory prefix of the recipe URL path (recorded scope fact).
    path_prefix: String,
    referer: Option<String>,
    user_agent: Option<String>,
}

impl UpstreamRecipe {
    /// Builds a recipe from a full upstream URL plus optional scoped headers.
    pub fn new(
        url: &str,
        referer: Option<String>,
        user_agent: Option<String>,
    ) -> Result<Self, ScopeError> {
        let parsed = url::Url::parse(url).map_err(|_| ScopeError::UnsupportedScheme)?;
        match parsed.scheme() {
            "http" | "https" => {}
            _ => return Err(ScopeError::UnsupportedScheme),
        }
        let host = parsed.host_str().ok_or(ScopeError::UnsupportedScheme)?;
        let mut origin = format!("{}://{host}", parsed.scheme());
        if let Some(port) = parsed.port() {
            origin.push_str(&format!(":{port}"));
        }
        let path = parsed.path();
        let prefix = match path.rfind('/') {
            Some(index) => path[..=index].to_string(),
            None => "/".to_string(),
        };
        Ok(Self {
            url: Zeroizing::new(url.to_string()),
            origin,
            path_prefix: prefix,
            referer,
            user_agent,
        })
    }

    #[must_use]
    pub fn origin(&self) -> &str {
        &self.origin
    }

    #[must_use]
    pub fn path_prefix(&self) -> &str {
        &self.path_prefix
    }

    /// Full upstream URL for the trusted fetch path only.
    #[must_use]
    pub fn url_for_upstream(&self) -> &str {
        &self.url
    }

    /// Scoped headers allowed on same-origin requests. Cookie/Authorization
    /// cannot appear here by construction.
    #[must_use]
    pub fn scoped_headers(&self) -> Vec<(&'static str, &str)> {
        let mut headers = Vec::new();
        if let Some(referer) = &self.referer {
            headers.push(("Referer", referer.as_str()));
        }
        if let Some(ua) = &self.user_agent {
            headers.push(("User-Agent", ua.as_str()));
        }
        headers
    }

    /// Resolves a (possibly relative) resource URI against the recipe URL;
    /// the result must stay on the recipe origin.
    pub fn resolve(&self, uri: &str) -> Result<String, ScopeError> {
        let base = url::Url::parse(&self.url).map_err(|_| ScopeError::UnsupportedScheme)?;
        let joined = base.join(uri).map_err(|_| ScopeError::UnsupportedScheme)?;
        match joined.scheme() {
            "http" | "https" => {}
            _ => return Err(ScopeError::UnsupportedScheme),
        }
        if origin_of(&joined) != self.origin {
            return Err(ScopeError::OriginEscape);
        }
        Ok(joined.to_string())
    }

    /// Per-hop header scope for redirects (RL-015): same-origin hops carry
    /// scoped headers, cross-origin hops strip them.
    pub fn header_scope_for(&self, next_url: &str) -> Result<HopScope, ScopeError> {
        let parsed = url::Url::parse(next_url).map_err(|_| ScopeError::UnsupportedScheme)?;
        match parsed.scheme() {
            "http" | "https" => {}
            _ => return Err(ScopeError::UnsupportedScheme),
        }
        if origin_of(&parsed) == self.origin {
            Ok(HopScope::CarryHeaders)
        } else {
            Ok(HopScope::StripHeaders)
        }
    }
}

impl Debug for UpstreamRecipe {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("UpstreamRecipe")
            .field("origin", &self.origin)
            .field("path_prefix", &self.path_prefix)
            .field("has_referer", &self.referer.is_some())
            .field("has_user_agent", &self.user_agent.is_some())
            .finish_non_exhaustive()
    }
}

fn origin_of(parsed: &url::Url) -> String {
    let mut origin = format!("{}://{}", parsed.scheme(), parsed.host_str().unwrap_or(""));
    if let Some(port) = parsed.port() {
        origin.push_str(&format!(":{port}"));
    }
    origin
}

struct VaultEntry {
    resource: ResourceId,
    recipe: UpstreamRecipe,
}

/// Per-session recipe store. Revocation drops and zeroizes everything.
#[derive(Default)]
pub struct RecipeVault {
    sessions: HashMap<SessionId, Vec<VaultEntry>>,
}

impl RecipeVault {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Stores a recipe for one session resource (idempotent re-store
    /// replaces). Bounded per session.
    pub fn store(
        &mut self,
        session: &SessionId,
        resource: ResourceId,
        recipe: UpstreamRecipe,
    ) -> Result<(), ScopeError> {
        let entries = self.sessions.entry(session.clone()).or_default();
        if let Some(existing) = entries.iter_mut().find(|e| e.resource == resource) {
            existing.recipe = recipe;
            return Ok(());
        }
        if entries.len() >= MAX_RECIPES_PER_SESSION {
            return Err(ScopeError::CapacityExceeded);
        }
        entries.push(VaultEntry { resource, recipe });
        Ok(())
    }

    #[must_use]
    pub fn get(&self, session: &SessionId, resource: &ResourceId) -> Option<&UpstreamRecipe> {
        self.sessions
            .get(session)?
            .iter()
            .find(|e| e.resource == *resource)
            .map(|e| &e.recipe)
    }

    /// Revokes one session: all its recipes are dropped (URLs zeroized).
    /// Idempotent; returns the number of dropped recipes.
    pub fn revoke_session(&mut self, session: &SessionId) -> usize {
        self.sessions
            .remove(session)
            .map(|entries| entries.len())
            .unwrap_or(0)
    }

    /// Revokes everything (navigation/profile/app-exit triggers).
    pub fn revoke_all(&mut self) {
        self.sessions.clear();
    }

    #[must_use]
    pub fn session_len(&self, session: &SessionId) -> usize {
        self.sessions.get(session).map_or(0, Vec::len)
    }
}

impl Debug for RecipeVault {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("RecipeVault")
            .field("sessions", &self.sessions.len())
            .finish_non_exhaustive()
    }
}
