//! Exact same-origin LAN target constraints, not a source of user authority.

use crate::ProbeHttpError;
use std::time::Duration;
use url::{Host, Url};

/// One entire selected LAN inspection, including HEAD and Range.
pub const SELECTED_LAN_PROBE_TIMEOUT: Duration = Duration::from_secs(15);

/// An exact RFC1918 literal URL on the current page's origin. Not Clone or
/// Debug: consumption is one-shot and URLs must not enter diagnostics.
///
/// This validates network constraints only. The caller must independently
/// authorize current Browser-verified playback and an explicit device choice.
/// It is not an Agent grant and must never be constructed from a network fact
/// alone. Hostnames are excluded, so the exception cannot permit DNS rebinding.
pub struct SameOriginLanTarget {
    url: Url,
}

impl SameOriginLanTarget {
    pub fn new(page_url: &str, media_url: &str) -> Result<Self, ProbeHttpError> {
        let page = parse_credential_free_http(page_url)?;
        let media = parse_credential_free_http(media_url)?;
        if page.origin() != media.origin() || media.fragment().is_some() {
            return Err(ProbeHttpError::InvalidUrl);
        }
        match media.host() {
            Some(Host::Ipv4(ip)) if ip.is_private() => Ok(Self { url: media }),
            _ => Err(ProbeHttpError::NonPublicAddress),
        }
    }

    pub(crate) fn url(&self) -> &str {
        self.url.as_str()
    }

    pub(crate) fn matches(&self, url: &Url) -> bool {
        self.url == *url
    }
}

pub(crate) fn parse_credential_free_http(value: &str) -> Result<Url, ProbeHttpError> {
    let parsed = Url::parse(value).map_err(|_| ProbeHttpError::InvalidUrl)?;
    if !matches!(parsed.scheme(), "http" | "https") {
        return Err(ProbeHttpError::UnsupportedScheme);
    }
    if !parsed.username().is_empty() || parsed.password().is_some() || parsed.port() == Some(0) {
        return Err(ProbeHttpError::InvalidUrl);
    }
    Ok(parsed)
}
