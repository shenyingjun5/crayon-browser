//! MP4 serving over the network guard (MED-13).
//!
//! Semantics (RL-009): client Range is forwarded; upstream 200/206/416 map
//! through with the whitelisted headers; malformed Range is ignored (full
//! body), matching RFC 7233. Bodies stream — backpressure flows through the
//! axum body stream; a stalled upstream is cut by a read-idle timeout
//! (RL-012). No body ever lands in memory whole.

use crate::network_guard::NetworkGuard;
use crate::router::{
    FetchError, FetchPlan, FetchRequest, FetchedMedia, ResourceFetcher, RouteKind,
};
use axum::body::Body;
use futures_util::StreamExt;
use std::future::Future;
use std::pin::Pin;
use std::time::Duration;

/// Default read-idle timeout between upstream chunks (RL-012 stall guard).
pub const DEFAULT_READ_IDLE_TIMEOUT: Duration = Duration::from_secs(30);

/// Response headers forwarded to the receiver (whitelist; everything else —
/// `set-cookie`, CSP, etc. — is dropped).
const FORWARDED_HEADERS: &[&str] = &[
    "content-type",
    "content-length",
    "content-range",
    "accept-ranges",
    "etag",
    "last-modified",
    "cache-control",
];

/// MP4 resource fetcher.
pub struct Mp4Fetcher {
    guard: NetworkGuard,
    read_idle_timeout: Duration,
}

impl Mp4Fetcher {
    #[must_use]
    pub fn new(guard: NetworkGuard) -> Self {
        Self {
            guard,
            read_idle_timeout: DEFAULT_READ_IDLE_TIMEOUT,
        }
    }

    #[must_use]
    pub const fn with_read_idle_timeout(mut self, timeout: Duration) -> Self {
        self.read_idle_timeout = timeout;
        self
    }
}

impl ResourceFetcher for Mp4Fetcher {
    fn fetch(
        &self,
        kind: RouteKind,
        plan: FetchPlan,
        request: FetchRequest,
    ) -> Pin<Box<dyn Future<Output = Result<FetchedMedia, FetchError>> + Send>> {
        let guard = self.guard.clone();
        let read_idle_timeout = self.read_idle_timeout;
        let result = async move {
            if kind != RouteKind::Resource {
                return Err(FetchError::NotFound);
            }
            let method = if request.method == "HEAD" {
                reqwest::Method::HEAD
            } else {
                reqwest::Method::GET
            };
            let mut headers = plan.headers.clone();
            if let Some(range) = &request.range {
                if is_well_formed_range(range) {
                    headers.push(("Range".to_string(), range.clone()));
                }
            }
            let fetch = guard
                .fetch_with(method, &plan.url, &headers, &plan.allow_set)
                .await
                .map_err(|_| FetchError::Upstream)?;
            let response = fetch.response;
            let status = response.status().as_u16();
            if status == 404 {
                return Err(FetchError::NotFound);
            }
            if !(200..300).contains(&status) && status != 416 {
                return Err(FetchError::Upstream);
            }
            let mut out_headers = Vec::new();
            for name in FORWARDED_HEADERS {
                if let Some(value) = response.headers().get(*name) {
                    if let Ok(value) = value.to_str() {
                        out_headers.push((name.to_string(), value.to_string()));
                    }
                }
            }
            let body = if request.method == "HEAD" {
                Body::empty()
            } else {
                stream_body(response, read_idle_timeout)
            };
            Ok(FetchedMedia {
                status,
                headers: out_headers,
                body,
            })
        };
        Box::pin(result)
    }
}

/// `bytes=a-b` / `bytes=a-` / `bytes=-n` only; anything else is ignored
/// (full body), never an error.
fn is_well_formed_range(value: &str) -> bool {
    let Some(spec) = value.strip_prefix("bytes=") else {
        return false;
    };
    let Some((start, end)) = spec.split_once('-') else {
        return false;
    };
    (start.is_empty() || start.bytes().all(|b| b.is_ascii_digit()))
        && (end.is_empty() || end.bytes().all(|b| b.is_ascii_digit()))
        && !(start.is_empty() && end.is_empty())
}

/// Wraps the upstream byte stream with a per-chunk idle timeout; a stall
/// ends the stream with an error instead of hanging (RL-012).
fn stream_body(response: reqwest::Response, idle: Duration) -> Body {
    let stream = futures_util::stream::unfold(
        (response.bytes_stream(), idle, false),
        |(mut inner, idle, done)| async move {
            if done {
                return None;
            }
            match tokio::time::timeout(idle, inner.next()).await {
                Ok(Some(Ok(bytes))) => Some((Ok(bytes), (inner, idle, false))),
                Ok(Some(Err(error))) => Some((Err(axum::Error::new(error)), (inner, idle, true))),
                Ok(None) => None,
                Err(_elapsed) => Some((
                    Err(axum::Error::new(std::io::Error::new(
                        std::io::ErrorKind::TimedOut,
                        "upstream read idle timeout",
                    ))),
                    (inner, idle, true),
                )),
            }
        },
    );
    Body::from_stream(stream)
}
