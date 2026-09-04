//! Bounded probe HTTP client (MED-05).
//!
//! Contract:
//! - no secrets: the client has no API for Cookie/Authorization and never
//!   forwards ambient credentials; only an explicit User-Agent is sent;
//! - no automatic redirects: 3xx is returned to the caller (per-hop redirect
//!   validation belongs to the relay network guard, MED-12);
//! - bounded: connect/total timeouts, byte-capped range reads;
//! - DNS safety: hostnames are resolved first, every resolved address must be
//!   publicly routable (private/loopback/link-local/CGNAT/benchmark/etc. are
//!   refused, including mixed answers), and the connection pins the validated
//!   address so DNS cannot be rebound between check and connect.

use crate::lan::{parse_credential_free_http, SameOriginLanTarget};
use std::fmt::{Display, Formatter};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};
use std::time::Duration;

/// Default connect timeout.
pub const DEFAULT_CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
/// Default total request timeout (probe reads are small and capped).
pub const DEFAULT_TOTAL_TIMEOUT: Duration = Duration::from_secs(15);
/// Default maximum body bytes collected by `range_get`.
pub const DEFAULT_MAX_BODY_BYTES: usize = 256 * 1024;
/// Probe requests carry no credentials; keep the UA aligned with the product
/// desktop UA so signature-bound URLs stay consistent.
const PROBE_UA: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

/// Probe client configuration (timeouts and byte caps).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ProbeHttpConfig {
    pub connect_timeout: Duration,
    pub total_timeout: Duration,
    pub max_body_bytes: usize,
    /// Test hook: allow loopback/private targets (local fixture servers).
    /// Never enable in product paths — it disables the SSRF posture.
    pub allow_private_addresses: bool,
}

impl Default for ProbeHttpConfig {
    fn default() -> Self {
        Self {
            connect_timeout: DEFAULT_CONNECT_TIMEOUT,
            total_timeout: DEFAULT_TOTAL_TIMEOUT,
            max_body_bytes: DEFAULT_MAX_BODY_BYTES,
            allow_private_addresses: false,
        }
    }
}

/// Probe transport failure. Ordinary network failures stay ordinary: no
/// variant carries partial data or implies any elevated trust (PL-014).
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ProbeHttpError {
    /// Only http/https URLs are probed.
    UnsupportedScheme,
    /// Host is (or resolves to) a non-publicly-routable address.
    NonPublicAddress,
    /// DNS resolution failed or returned no usable address.
    Dns,
    /// TCP/TLS connect failed.
    Connect,
    /// Connect or total timeout expired.
    Timeout,
    /// Transport error after connect.
    Transport,
    /// URL is malformed.
    InvalidUrl,
    /// Range is empty or overflows the byte offset.
    InvalidRange,
    /// A scoped request attempted to change its exact URL.
    ScopeMismatch,
}

impl Display for ProbeHttpError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::UnsupportedScheme => "unsupported url scheme",
            Self::NonPublicAddress => "address is not publicly routable",
            Self::Dns => "dns resolution failed",
            Self::Connect => "connect failed",
            Self::Timeout => "probe timed out",
            Self::Transport => "transport error",
            Self::InvalidUrl => "malformed url",
            Self::InvalidRange => "invalid byte range",
            Self::ScopeMismatch => "probe target outside scope",
        };
        f.write_str(message)
    }
}

impl std::error::Error for ProbeHttpError {}

/// Bounded probe response.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ProbeResponse {
    pub status: u16,
    pub content_type: Option<String>,
    pub content_length: Option<u64>,
    pub accept_ranges: bool,
    /// Body bytes (only for `range_get`), capped at `max_body_bytes`.
    pub body: Vec<u8>,
}

/// Conservative public-routability check (denylist posture).
#[must_use]
pub fn is_publicly_routable(addr: &IpAddr) -> bool {
    match addr {
        IpAddr::V4(v4) => is_public_v4(v4),
        IpAddr::V6(v6) => is_public_v6(v6),
    }
}

fn is_public_v4(ip: &Ipv4Addr) -> bool {
    let o = ip.octets();
    !(ip.is_private()
        || ip.is_loopback()
        || ip.is_link_local()
        || ip.is_unspecified()
        || ip.is_broadcast()
        || ip.is_multicast()
        || ip.is_documentation()
        // CGNAT 100.64.0.0/10
        || (o[0] == 100 && (o[1] & 0xC0) == 64)
        // benchmarking 198.18.0.0/15
        || (o[0] == 198 && (o[1] & 0xFE) == 18)
        // reserved 240.0.0.0/4
        || (o[0] & 0xF0) == 240)
}

fn is_public_v6(ip: &Ipv6Addr) -> bool {
    // The OS can connect mapped IPv6 literals as IPv4. Apply the same
    // address policy before either literal connect or DNS pinning.
    if let Some(mapped) = ip.to_ipv4_mapped() {
        return is_public_v4(&mapped);
    }
    !(ip.is_loopback()
        || ip.is_unspecified()
        || ip.is_multicast()
        // ULA fc00::/7
        || (ip.segments()[0] & 0xFE00) == 0xFC00
        // link-local fe80::/10
        || (ip.segments()[0] & 0xFFC0) == 0xFE80)
}

/// Every resolved address must be publicly routable; mixed answers (public +
/// private) are rejected as a class (DNS rebinding posture).
pub fn validate_resolved(addrs: &[IpAddr]) -> Result<(), ProbeHttpError> {
    validate_resolved_inner(addrs, false)
}

fn validate_resolved_inner(addrs: &[IpAddr], allow_private: bool) -> Result<(), ProbeHttpError> {
    if addrs.is_empty() {
        return Err(ProbeHttpError::Dns);
    }
    if !allow_private && addrs.iter().any(|a| !is_publicly_routable(a)) {
        return Err(ProbeHttpError::NonPublicAddress);
    }
    Ok(())
}

/// Bounded probe client. One client per probe batch is fine; configuration
/// is fixed at construction.
pub struct ProbeHttpClient {
    config: ProbeHttpConfig,
    selected_lan_target: Option<SameOriginLanTarget>,
}

impl ProbeHttpClient {
    #[must_use]
    pub fn new(config: ProbeHttpConfig) -> Self {
        Self {
            config,
            selected_lan_target: None,
        }
    }

    /// Only the consuming inspector can create a scoped client; ordinary
    /// head/range callers cannot enlarge their address policy.
    pub(crate) fn for_selected_lan(&self, target: SameOriginLanTarget) -> Self {
        Self {
            config: ProbeHttpConfig {
                allow_private_addresses: false,
                ..self.config
            },
            selected_lan_target: Some(target),
        }
    }

    /// HEAD with no redirect following. 3xx is surfaced, not followed.
    pub async fn head(&self, url: &str) -> Result<ProbeResponse, ProbeHttpError> {
        let request = self.prepare(reqwest::Method::HEAD, url).await?;
        self.send(request, 0).await
    }

    /// GET with `Range: bytes=start..` collecting at most `max_len` bytes
    /// (hard-capped by config). Used for container sniffing without
    /// downloading the body (PL-003).
    pub async fn range_get(
        &self,
        url: &str,
        start: u64,
        max_len: usize,
    ) -> Result<ProbeResponse, ProbeHttpError> {
        let cap = max_len.min(self.config.max_body_bytes);
        let end = u64::try_from(cap)
            .ok()
            .and_then(|len| len.checked_sub(1))
            .and_then(|offset| start.checked_add(offset))
            .ok_or(ProbeHttpError::InvalidRange)?;
        let request = self.prepare(reqwest::Method::GET, url).await?;
        let request = request.header(reqwest::header::RANGE, format!("bytes={start}-{end}"));
        self.send(request, cap).await
    }

    async fn prepare(
        &self,
        method: reqwest::Method,
        url: &str,
    ) -> Result<reqwest::RequestBuilder, ProbeHttpError> {
        let parsed = parse_credential_free_http(url)?;
        if let Some(target) = &self.selected_lan_target {
            if !target.matches(&parsed) {
                return Err(ProbeHttpError::ScopeMismatch);
            }
        }
        let host = parsed.host_str().ok_or(ProbeHttpError::InvalidUrl)?;

        // Literal IP: classify directly. Hostname: resolve, classify every
        // answer, pin the first public address for the actual connect.
        let host_ip: Option<IpAddr> = host
            .trim_start_matches('[')
            .trim_end_matches(']')
            .parse()
            .ok();
        let mut builder = reqwest::Client::builder()
            // Probes must connect to the validated destination, never an
            // ambient proxy that can resolve elsewhere or receive private URLs.
            .no_proxy()
            .redirect(reqwest::redirect::Policy::none())
            .connect_timeout(self.config.connect_timeout)
            .timeout(self.config.total_timeout)
            .user_agent(PROBE_UA);
        if let Some(ip) = host_ip {
            if self.selected_lan_target.is_none()
                && !self.config.allow_private_addresses
                && !is_publicly_routable(&ip)
            {
                return Err(ProbeHttpError::NonPublicAddress);
            }
        } else {
            let port = parsed.port_or_known_default().unwrap_or(443);
            let addrs: Vec<IpAddr> = tokio::net::lookup_host((host, port))
                .await
                .map_err(|_| ProbeHttpError::Dns)?
                .map(|sa| sa.ip())
                .collect();
            validate_resolved_inner(&addrs, self.config.allow_private_addresses)?;
            let pinned = addrs[0];
            builder = builder.resolve(host, std::net::SocketAddr::new(pinned, port));
        }
        let client = builder.build().map_err(|_| ProbeHttpError::Transport)?;
        Ok(client.request(method, url))
    }

    async fn send(
        &self,
        request: reqwest::RequestBuilder,
        body_cap: usize,
    ) -> Result<ProbeResponse, ProbeHttpError> {
        let response = request.send().await.map_err(classify_reqwest)?;
        let status = response.status().as_u16();
        let headers = response.headers();
        let content_type = headers
            .get(reqwest::header::CONTENT_TYPE)
            .and_then(|v| v.to_str().ok())
            .map(str::to_string);
        let content_length = headers
            .get(reqwest::header::CONTENT_LENGTH)
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.parse().ok());
        let accept_ranges = headers
            .get(reqwest::header::ACCEPT_RANGES)
            .and_then(|v| v.to_str().ok())
            .is_some_and(|v| v.eq_ignore_ascii_case("bytes"));

        let mut body = Vec::new();
        if body_cap > 0 {
            let mut stream = response.bytes_stream();
            use futures_util::StreamExt;
            while let Some(chunk) = stream.next().await {
                let chunk = chunk.map_err(classify_reqwest)?;
                let remaining = body_cap - body.len();
                if remaining == 0 {
                    break;
                }
                body.extend_from_slice(&chunk[..chunk.len().min(remaining)]);
                if body.len() >= body_cap {
                    break;
                }
            }
        }
        Ok(ProbeResponse {
            status,
            content_type,
            content_length,
            accept_ranges,
            body,
        })
    }
}

fn classify_reqwest(error: reqwest::Error) -> ProbeHttpError {
    if error.is_timeout() {
        ProbeHttpError::Timeout
    } else if error.is_connect() {
        ProbeHttpError::Connect
    } else {
        ProbeHttpError::Transport
    }
}

#[cfg(test)]
#[path = "../tests/support/lan_transport.rs"]
mod lan_transport;
