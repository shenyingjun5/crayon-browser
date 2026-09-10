//! Outbound network policy (HUB-12).
//!
//! The policy layer composes allowlisting, DNS re-validation and SSRF
//! guards around host-injected IO. Real DNS resolution and socket exchange
//! are `ResolverPort`/`ExchangePort` implementations owned by the product
//! runtime (HUB-14 assembly); this module decides what may be contacted
//! and with what budgets. All checks fail closed.

use std::net::IpAddr;

use crate::api::{ConnectorCall, ConnectorError, NetworkPort};

/// Maximum redirects followed for one call.
pub const MAX_REDIRECT_HOPS: usize = 3;
/// Maximum response bytes accepted from one exchange.
pub const MAX_RESPONSE_BYTES: usize = 1024 * 1024;

/// A DNS-resolved address fed by the host resolver. Built from
/// `std::net::IpAddr` so the classification uses std semantics.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ResolvedAddress {
    ip: IpAddr,
}

impl ResolvedAddress {
    #[must_use]
    pub fn new(ip: IpAddr) -> Self {
        Self { ip }
    }

    /// SSRF guard: loopback, private, link-local (including the cloud
    /// metadata service), unique-local and unspecified addresses are all
    /// rejected. Only global unicast space passes.
    #[must_use]
    pub fn is_public(&self) -> bool {
        match self.ip {
            IpAddr::V4(v4) => {
                !v4.is_loopback()
                    && !v4.is_private()
                    && !v4.is_link_local()
                    && !v4.is_unspecified()
                    && !v4.is_broadcast()
                    && !v4.is_documentation()
                    // The metadata endpoint inside the link-local block.
                    && v4.octets() != [169, 254, 169, 254]
            }
            IpAddr::V6(v6) => {
                let unique_local = v6.segments()[0] & 0xfe00 == 0xfc00; // fc00::/7 (top 7 bits)
                let mapped_v4_rejected = v6
                    .to_ipv4_mapped()
                    .is_some_and(|v4| v4.is_loopback() || v4.is_private());
                !v6.is_loopback() && !v6.is_unspecified() && !unique_local && !mapped_v4_rejected
            }
        }
    }
}

/// Exact endpoint-reference allowlist, host-populated.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct EndpointAllowlist {
    entries: Vec<String>,
}

impl EndpointAllowlist {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Registers one exact endpoint reference. Idempotent.
    pub fn register(&mut self, endpoint_ref: &str) {
        if !endpoint_ref.is_empty() && !self.entries.iter().any(|e| e == endpoint_ref) {
            self.entries.push(endpoint_ref.to_owned());
        }
    }

    #[must_use]
    pub fn contains(&self, endpoint_ref: &str) -> bool {
        self.entries.iter().any(|e| e == endpoint_ref)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

/// Host-injected DNS resolution: returns every resolved address for the
/// host of a call; resolution failure is `Err`.
pub trait ResolverPort {
    fn resolve(&mut self, host: &str) -> Result<Vec<ResolvedAddress>, ConnectorError>;
}

/// One HTTP-style exchange the host performs after policy acceptance.
pub struct ExchangeRequest<'a> {
    pub host: &'a str,
    pub path: &'a str,
    pub payload: &'a str,
}

/// Host-injected exchange result: response body plus an optional
/// redirect target (the next URL to policy-check).
pub struct ExchangeResponse {
    pub body: String,
    pub redirect: Option<String>,
    pub truncated: bool,
}

/// Host-injected IO for one already-cleared exchange.
pub trait ExchangePort {
    fn exchange(
        &mut self,
        request: &ExchangeRequest<'_>,
    ) -> Result<ExchangeResponse, ConnectorError>;
}

/// Network policy failure; content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NetworkPolicyError {
    /// The endpoint reference is not registered.
    EndpointNotAllowed,
    /// A resolved address failed the SSRF guard (DNS re-validation).
    AddressForbidden,
    /// The exchange target left the registered host or the redirect
    /// budget was exhausted.
    RedirectForbidden,
    /// The response exceeded the byte budget.
    ResponseTooLarge,
    /// The resolver or exchange failed.
    Transport,
}

/// `NetworkPort` implementation composing the full guard chain.
pub struct PolicyNetworkPort<R: ResolverPort, X: ExchangePort> {
    allowlist: EndpointAllowlist,
    resolver: R,
    exchange: X,
}

impl<R: ResolverPort, X: ExchangePort> PolicyNetworkPort<R, X> {
    #[must_use]
    pub fn new(allowlist: EndpointAllowlist, resolver: R, exchange: X) -> Self {
        Self {
            allowlist,
            resolver,
            exchange,
        }
    }

    #[must_use]
    pub fn allowlist(&self) -> &EndpointAllowlist {
        &self.allowlist
    }

    /// DNS re-validation: every resolved address must be public; one
    /// private address among many still rejects (rebinding guard).
    fn validate_resolution(&mut self, host: &str) -> Result<Vec<ResolvedAddress>, ConnectorError> {
        let addresses = self.resolver.resolve(host)?;
        if addresses.is_empty() {
            return Err(ConnectorError::TargetForbidden);
        }
        if addresses.iter().all(ResolvedAddress::is_public) {
            Ok(addresses)
        } else {
            Err(ConnectorError::TargetForbidden)
        }
    }
}

impl<R: ResolverPort, X: ExchangePort> NetworkPort for PolicyNetworkPort<R, X> {
    /// Executes one call through the full guard chain: allowlist →
    /// resolve → SSRF → exchange → redirect loop (each hop re-validated,
    /// bounded) → response budget.
    fn send(&mut self, call: &ConnectorCall<'_>) -> Result<String, ConnectorError> {
        if !self.allowlist.contains(call.endpoint_ref()) {
            return Err(ConnectorError::TargetForbidden);
        }
        let mut host = call.endpoint_ref().to_owned();
        let mut payload = call.payload().to_owned();
        let mut path = String::from("/");
        for hops_left in (0..=MAX_REDIRECT_HOPS).rev() {
            self.validate_resolution(&host)?;
            let response = self.exchange.exchange(&ExchangeRequest {
                host: &host,
                path: &path,
                payload: &payload,
            })?;
            if response.body.len() > MAX_RESPONSE_BYTES || response.truncated {
                return Err(ConnectorError::PayloadTooLarge);
            }
            match &response.redirect {
                None => return Ok(response.body),
                Some(next) => {
                    if hops_left == 0 {
                        return Err(ConnectorError::TargetForbidden);
                    }
                    // Each hop must stay on the registered host; the path
                    // may change. Anything else is a redirect escape.
                    let (next_host, next_path) = split_redirect(next, call.endpoint_ref())?;
                    host = next_host;
                    path = next_path;
                    payload.clear();
                }
            }
        }
        Err(ConnectorError::TargetForbidden)
    }
}

/// Splits a redirect target into (host, path). The target must stay on the
/// registered host and scheme-less references keep the host form used by
/// this layer (opaque endpoint refs, not free-form URLs).
fn split_redirect(target: &str, registered: &str) -> Result<(String, String), ConnectorError> {
    if target.is_empty() {
        return Err(ConnectorError::TargetForbidden);
    }
    // Absolute URL: only the registered authority passes.
    if let Some(rest) = target.strip_prefix("https://") {
        let (authority, path) = match rest.split_once('/') {
            Some((authority, path)) => (authority, format!("/{path}")),
            None => (rest, "/".to_owned()),
        };
        if authority == registered {
            return Ok((authority.to_owned(), path));
        }
        return Err(ConnectorError::TargetForbidden);
    }
    // Same-host relative path.
    if target.starts_with('/') {
        return Ok((registered.to_owned(), target.to_owned()));
    }
    Err(ConnectorError::TargetForbidden)
}

#[cfg(test)]
mod network_tests;
