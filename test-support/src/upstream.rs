//! `MockUpstream`: scripted media upstream for relay/probe integration tests.
//!
//! Covers MP4/HLS/DASH bodies, Range observation, redirects, slow/stalled
//! responses (drip control) and request recording. Loopback + random port only.

use crate::server::{DripGate, MiniServer, RawBody, RawResponse, RecordedRequest};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

/// A recorded request seen by the upstream.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct UpstreamRequest {
    pub method: String,
    pub path: String,
    headers: Vec<(String, String)>,
}

impl UpstreamRequest {
    /// First header value matching `name` (case-insensitive), e.g. `range`.
    #[must_use]
    pub fn header(&self, name: &str) -> Option<&str> {
        self.headers
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(name))
            .map(|(_, v)| v.as_str())
    }
}

/// Test-side handle controlling a drip response (release chunks explicitly;
/// no timers involved).
#[derive(Clone)]
pub struct DripControl {
    gate: Arc<DripGate>,
}

impl DripControl {
    /// Allows `n` more chunks to flow to the client.
    pub fn release(&self, n: usize) {
        self.gate.release(n);
    }
}

/// Scripted upstream behaviour for one path.
#[derive(Clone)]
pub enum UpstreamScript {
    /// Single full response.
    Full {
        status: u16,
        content_type: Option<String>,
        body: Vec<u8>,
    },
    /// 302 redirect to `location` (per-hop redirect validation tests).
    Redirect { location: String },
    /// Body delivered chunk by chunk, each chunk requiring an explicit
    /// `DripControl::release` — deterministic slow-response/stall simulation.
    Drip {
        status: u16,
        content_type: Option<String>,
        chunks: Vec<Vec<u8>>,
        control: DripControl,
    },
    /// Range-aware full body: a valid `Range: bytes=a-b` request gets 206 +
    /// `Content-Range` + the slice (MP4 seek path); otherwise 200 full body.
    /// Always advertises `Accept-Ranges: bytes`. Out-of-range gets 416-style
    /// 200 full body (simplest honest fallback for a test double).
    RangeAware {
        content_type: Option<String>,
        body: Vec<u8>,
    },
}

/// Creates a drip script plus its test-side control handle.
#[must_use]
pub fn drip(
    status: u16,
    content_type: Option<String>,
    chunks: Vec<Vec<u8>>,
) -> (UpstreamScript, DripControl) {
    let control = DripControl {
        gate: Arc::new(DripGate::default()),
    };
    (
        UpstreamScript::Drip {
            status,
            content_type,
            chunks,
            control: control.clone(),
        },
        control,
    )
}

/// Parses a single `bytes=a-b`/`bytes=a-` range against `len`, returning the
/// inclusive (start, end) pair; unsatisfiable/malformed ranges return `None`.
fn parse_byte_range(header: Option<&str>, len: usize) -> Option<(usize, usize)> {
    let spec = header?.strip_prefix("bytes=")?;
    let (start, end) = spec.split_once('-')?;
    let start: usize = start.parse().ok()?;
    if start >= len {
        return None;
    }
    let end = if end.is_empty() {
        len - 1
    } else {
        end.parse::<usize>().ok()?.min(len - 1)
    };
    if end < start {
        return None;
    }
    Some((start, end))
}

struct State {
    routes: HashMap<String, UpstreamScript>,
}

/// Scripted upstream server. Routes are exact path matches; unregistered
/// paths return 404 (never fall through to a real network).
pub struct MockUpstream {
    server: MiniServer,
    state: Arc<Mutex<State>>,
}

impl MockUpstream {
    /// Starts an upstream with the given route table.
    pub async fn start(routes: Vec<(String, UpstreamScript)>) -> std::io::Result<Self> {
        let state = Arc::new(Mutex::new(State {
            routes: routes.into_iter().collect(),
        }));
        let handler_state = state.clone();
        let server = MiniServer::start(Arc::new(move |request: &RecordedRequest| {
            let state = handler_state.lock().unwrap();
            match state.routes.get(&request.path) {
                Some(UpstreamScript::Full {
                    status,
                    content_type,
                    body,
                }) => RawResponse {
                    status: *status,
                    headers: content_type
                        .iter()
                        .map(|ct| ("Content-Type".to_string(), ct.clone()))
                        .collect(),
                    body: RawBody::Full(body.clone()),
                },
                Some(UpstreamScript::Redirect { location }) => RawResponse {
                    status: 302,
                    headers: vec![("Location".to_string(), location.clone())],
                    body: RawBody::Full(Vec::new()),
                },
                Some(UpstreamScript::Drip {
                    status,
                    content_type,
                    chunks,
                    control,
                }) => RawResponse {
                    status: *status,
                    headers: content_type
                        .iter()
                        .map(|ct| ("Content-Type".to_string(), ct.clone()))
                        .collect(),
                    body: RawBody::Drip(chunks.clone(), control.gate.clone()),
                },
                Some(UpstreamScript::RangeAware { content_type, body }) => {
                    let mut headers: Vec<(String, String)> = content_type
                        .iter()
                        .map(|ct| ("Content-Type".to_string(), ct.clone()))
                        .collect();
                    headers.push(("Accept-Ranges".to_string(), "bytes".to_string()));
                    match parse_byte_range(request.header("range"), body.len()) {
                        Some((start, end)) => {
                            headers.push((
                                "Content-Range".to_string(),
                                format!("bytes {start}-{end}/{}", body.len()),
                            ));
                            RawResponse {
                                status: 206,
                                headers,
                                body: RawBody::Full(body[start..=end].to_vec()),
                            }
                        }
                        None => RawResponse {
                            status: 200,
                            headers,
                            body: RawBody::Full(body.clone()),
                        },
                    }
                }
                None => RawResponse {
                    status: 404,
                    headers: Vec::new(),
                    body: RawBody::Full(Vec::new()),
                },
            }
        }))
        .await?;
        Ok(Self { server, state })
    }

    /// Loopback base URL with the system-assigned random port.
    #[must_use]
    pub fn base_url(&self) -> String {
        self.server.base_url()
    }

    /// Absolute URL for a route path.
    #[must_use]
    pub fn url(&self, path: &str) -> String {
        format!("{}{}", self.base_url(), path)
    }

    /// Recorded requests in arrival order (bounded).
    #[must_use]
    pub fn requests(&self) -> Vec<UpstreamRequest> {
        self.server
            .requests()
            .into_iter()
            .map(|r| UpstreamRequest {
                method: r.method,
                path: r.path,
                headers: r.headers,
            })
            .collect()
    }

    /// Number of times a path was requested.
    #[must_use]
    pub fn hit_count(&self, path: &str) -> usize {
        self.server
            .requests()
            .iter()
            .filter(|r| r.path == path)
            .count()
    }

    /// Replaces or inserts a route at runtime (e.g. failover scenarios).
    pub fn set_route(&self, path: &str, script: UpstreamScript) {
        self.state
            .lock()
            .unwrap()
            .routes
            .insert(path.to_string(), script);
    }
}
