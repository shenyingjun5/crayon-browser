//! Relay network guard (MED-12): every upstream hop is validated before it
//! happens.
//!
//! Per hop, in order:
//! 1. scheme must be http/https;
//! 2. host must be inside the session allow-set (fixed at session creation;
//!    entries are `host` or `host:port`);
//! 3. DNS: hostnames resolve first, every answer must be publicly routable
//!    (mixed answers rejected), and the connection pins the validated
//!    address — a rebind between check and connect cannot redirect the
//!    connection (RL-007);
//! 4. redirects are followed manually (bounded hops); scoped headers
//!    (Referer/UA) are carried only to same-origin hops and stripped on
//!    cross-origin hops (RL-015);
//! 5. any rejection returns an error without surfacing internal responses
//!    (RL-006).

use crayon_media_probe::http::{is_publicly_routable, ProbeHttpError};
use std::fmt::{Display, Formatter};
use std::net::{IpAddr, SocketAddr};
use std::sync::Arc;
use std::time::Duration;

/// Default connect timeout per hop.
pub const DEFAULT_CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
/// Default bound for sending a request and reading the response head.
pub const DEFAULT_HEAD_TIMEOUT: Duration = Duration::from_secs(15);
/// Maximum redirect hops.
pub const MAX_HOPS: usize = 5;

/// DNS resolver seam: production uses the system resolver; tests inject
/// deterministic mappings.
pub type Resolver = Arc<dyn Fn(&str, u16) -> Result<Vec<IpAddr>, GuardError> + Send + Sync>;

/// Guard configuration.
#[derive(Clone)]
pub struct NetworkGuardConfig {
    pub connect_timeout: Duration,
    pub head_timeout: Duration,
    pub max_hops: usize,
    /// Test hook: skip public-routability classification (local fixtures).
    /// Never enable in product paths.
    pub allow_private_addresses: bool,
    pub resolver: Option<Resolver>,
}

impl Default for NetworkGuardConfig {
    fn default() -> Self {
        Self {
            connect_timeout: DEFAULT_CONNECT_TIMEOUT,
            head_timeout: DEFAULT_HEAD_TIMEOUT,
            max_hops: MAX_HOPS,
            allow_private_addresses: false,
            resolver: None,
        }
    }
}

/// Guard rejection. Ordinary network failures stay ordinary (PL-014).
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum GuardError {
    UnsupportedScheme,
    InvalidUrl,
    /// Host outside the session allow-set.
    NotAllowedHost,
    /// Non-publicly-routable target (literal or resolved).
    NonPublicAddress,
    Dns,
    TooManyHops,
    /// Redirect without a usable Location.
    BadRedirect,
    Connect,
    Timeout,
    Transport,
}

impl Display for GuardError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::UnsupportedScheme => "unsupported url scheme",
            Self::InvalidUrl => "malformed url",
            Self::NotAllowedHost => "host not in session allow-set",
            Self::NonPublicAddress => "address is not publicly routable",
            Self::Dns => "dns resolution failed",
            Self::TooManyHops => "too many redirect hops",
            Self::BadRedirect => "redirect without usable location",
            Self::Connect => "connect failed",
            Self::Timeout => "upstream timed out",
            Self::Transport => "transport error",
        };
        f.write_str(message)
    }
}

impl std::error::Error for GuardError {}

impl From<ProbeHttpError> for GuardError {
    fn from(error: ProbeHttpError) -> Self {
        match error {
            ProbeHttpError::NonPublicAddress => Self::NonPublicAddress,
            ProbeHttpError::Dns => Self::Dns,
            ProbeHttpError::Connect => Self::Connect,
            ProbeHttpError::Timeout => Self::Timeout,
            ProbeHttpError::UnsupportedScheme => Self::UnsupportedScheme,
            ProbeHttpError::InvalidUrl => Self::InvalidUrl,
            ProbeHttpError::Transport => Self::Transport,
        }
    }
}

/// A validated final response plus the URL it came from.
pub struct GuardedFetch {
    pub final_url: String,
    pub hops: usize,
    pub response: reqwest::Response,
}

impl std::fmt::Debug for GuardedFetch {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // URL 有意不出现（RL-014：query 可能携带签名）。
        f.debug_struct("GuardedFetch")
            .field("hops", &self.hops)
            .field("status", &self.response.status())
            .finish_non_exhaustive()
    }
}

/// The relay network guard.
#[derive(Clone)]
pub struct NetworkGuard {
    config: NetworkGuardConfig,
}

impl NetworkGuard {
    #[must_use]
    pub fn new(config: NetworkGuardConfig) -> Self {
        Self { config }
    }

    /// Fetches `url` with per-hop validation. `headers` are scoped headers
    /// (Referer/UA from the vault recipe); they follow the hop scope rules.
    pub async fn fetch(
        &self,
        url: &str,
        headers: &[(String, String)],
        allow_set: &[String],
    ) -> Result<GuardedFetch, GuardError> {
        self.fetch_with(reqwest::Method::GET, url, headers, allow_set)
            .await
    }

    /// Same as `fetch` with an explicit method (HEAD for existence probes).
    pub async fn fetch_with(
        &self,
        method: reqwest::Method,
        url: &str,
        headers: &[(String, String)],
        allow_set: &[String],
    ) -> Result<GuardedFetch, GuardError> {
        let mut current = url.to_string();
        let mut carry_headers = headers.to_vec();
        for hop in 0..self.config.max_hops {
            let response = self
                .fetch_hop(method.clone(), &current, &carry_headers, allow_set)
                .await?;
            let status = response.status();
            if !status.is_redirection() {
                return Ok(GuardedFetch {
                    final_url: current,
                    hops: hop,
                    response,
                });
            }
            let location = response
                .headers()
                .get(reqwest::header::LOCATION)
                .and_then(|v| v.to_str().ok())
                .ok_or(GuardError::BadRedirect)?
                .to_string();
            let next = url::Url::parse(&current)
                .map_err(|_| GuardError::InvalidUrl)?
                .join(&location)
                .map_err(|_| GuardError::BadRedirect)?
                .to_string();
            match next_url_scheme(&next)? {
                // 跨 origin 跳转：剥离 scoped headers（RL-015）
                _ if origin_of(&next) != origin_of(&current) => carry_headers = Vec::new(),
                _ => {}
            }
            current = next;
        }
        Err(GuardError::TooManyHops)
    }

    async fn fetch_hop(
        &self,
        method: reqwest::Method,
        url: &str,
        headers: &[(String, String)],
        allow_set: &[String],
    ) -> Result<reqwest::Response, GuardError> {
        let parsed = url::Url::parse(url).map_err(|_| GuardError::InvalidUrl)?;
        match parsed.scheme() {
            "http" | "https" => {}
            _ => return Err(GuardError::UnsupportedScheme),
        }
        let host = parsed.host_str().ok_or(GuardError::InvalidUrl)?;
        let port = parsed.port_or_known_default().unwrap_or(443);
        check_allow_set(host, port, allow_set)?;

        let mut builder = reqwest::Client::builder()
            .redirect(reqwest::redirect::Policy::none())
            .connect_timeout(self.config.connect_timeout);
        let literal: Option<IpAddr> = host
            .trim_start_matches('[')
            .trim_end_matches(']')
            .parse()
            .ok();
        match literal {
            Some(ip) => {
                if !self.config.allow_private_addresses && !is_publicly_routable(&ip) {
                    return Err(GuardError::NonPublicAddress);
                }
            }
            None => {
                let addrs = self.resolve(host, port).await?;
                if addrs.is_empty() {
                    return Err(GuardError::Dns);
                }
                if !self.config.allow_private_addresses
                    && addrs.iter().any(|a| !is_publicly_routable(a))
                {
                    return Err(GuardError::NonPublicAddress);
                }
                // RL-007：连接固定已校验地址，校验与连接之间无法重绑定。
                builder = builder.resolve(host, SocketAddr::new(addrs[0], port));
            }
        }
        let client = builder.build().map_err(|_| GuardError::Transport)?;
        let mut request = client.request(method, url);
        for (name, value) in headers {
            request = request.header(name, value);
        }
        let send = request.send();
        match tokio::time::timeout(self.config.head_timeout, send).await {
            Ok(Ok(response)) => Ok(response),
            Ok(Err(error)) => Err(classify(&error)),
            Err(_) => Err(GuardError::Timeout),
        }
    }

    async fn resolve(&self, host: &str, port: u16) -> Result<Vec<IpAddr>, GuardError> {
        if let Some(resolver) = &self.config.resolver {
            return resolver(host, port);
        }
        tokio::net::lookup_host((host, port))
            .await
            .map(|addrs| addrs.map(|sa| sa.ip()).collect())
            .map_err(|_| GuardError::Dns)
    }
}

fn classify(error: &reqwest::Error) -> GuardError {
    if error.is_timeout() {
        GuardError::Timeout
    } else if error.is_connect() {
        GuardError::Connect
    } else {
        GuardError::Transport
    }
}

fn next_url_scheme(url: &str) -> Result<(), GuardError> {
    let parsed = url::Url::parse(url).map_err(|_| GuardError::BadRedirect)?;
    match parsed.scheme() {
        "http" | "https" => Ok(()),
        _ => Err(GuardError::UnsupportedScheme),
    }
}

fn origin_of(url: &str) -> String {
    match url::Url::parse(url) {
        Ok(parsed) => {
            let mut origin = format!("{}://{}", parsed.scheme(), parsed.host_str().unwrap_or(""));
            if let Some(port) = parsed.port() {
                origin.push_str(&format!(":{port}"));
            }
            origin
        }
        Err(_) => String::new(),
    }
}

/// Allow-set entries are `host` (any port) or `host:port` (exact).
fn check_allow_set(host: &str, port: u16, allow_set: &[String]) -> Result<(), GuardError> {
    let host = host.to_ascii_lowercase();
    let host_port = format!("{host}:{port}");
    if allow_set.iter().any(|entry| {
        let entry = entry.to_ascii_lowercase();
        entry == host || entry == host_port
    }) {
        Ok(())
    } else {
        Err(GuardError::NotAllowedHost)
    }
}
