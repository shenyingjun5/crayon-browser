//! Relay routers (MED-11; design §11.2).
//!
//! Two routers with a strict surface:
//!
//! - control plane (loopback only, bound by the runtime in MED-16):
//!   `POST /internal/cast/session`, `DELETE /internal/cast/session/{token}`,
//!   `GET /internal/health`; authenticated by the per-process control secret;
//! - media plane (LAN): `GET /s/{token}/master.m3u8`,
//!   `GET /s/{token}/manifest.mpd`, `GET /s/{token}/r/{resource_id}/{name}`.
//!
//! There are no legacy extraction/proxy/player routes at all (RL-001) —
//! only the opaque session/resource surface below. Authorization always runs
//! before any upstream access (RL-003); malformed tokens/ids, path
//! traversal, wrong methods and oversized input are rejected bounded
//! (RL-008). Media bytes flow through the pluggable `ResourceFetcher`
//! (MP4/HLS streaming lands in MED-13/15).

use crate::session::{SessionAuthError, SessionRegistry};
use crate::vault::RecipeVault;
use axum::body::Body;
use axum::extract::{ConnectInfo, Path, State};
use axum::http::{header, HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{delete, get, post};
use axum::{Json, Router};
use crayon_domain::{DeviceId, ResourceId};
use serde::{Deserialize, Serialize};
use std::future::Future;
use std::net::SocketAddr;
use std::pin::Pin;
use std::sync::{Arc, Mutex};

/// Maximum control-plane body (bounded input rule, RL-008).
const MAX_CONTROL_BODY_BYTES: usize = 16 * 1024;
/// Header carrying the per-process control secret.
const CONTROL_SECRET_HEADER: &str = "x-crayon-control-secret";

/// Which media route is being served.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RouteKind {
    MasterPlaylist,
    DashManifest,
    Resource,
}

/// Trusted fetch hand-off built inside the vault lock; awaited outside it.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FetchPlan {
    pub url: String,
    pub headers: Vec<(String, String)>,
    /// Session-fixed upstream allow-set (cloned at authorization time).
    pub allow_set: Vec<String>,
    /// Session token hex (opaque route prefix for rewritten URIs).
    pub token_hex: String,
    /// Owning session id (vault writes).
    pub session_id: crayon_domain::SessionId,
    /// Nesting depth of this resource (master = 0).
    pub depth: u8,
}

/// Client request facts forwarded to the fetcher.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FetchRequest {
    /// `GET` or `HEAD` (router only routes these).
    pub method: String,
    /// Raw `Range` header value, if present.
    pub range: Option<String>,
    /// Raw `If-None-Match` header value, if present.
    pub if_none_match: Option<String>,
}

/// A fetched media response: mapped status/headers plus a streaming body
/// (backpressure flows through the body stream).
pub struct FetchedMedia {
    pub status: u16,
    pub headers: Vec<(String, String)>,
    pub body: Body,
}

/// Media fetch seam: implemented by the MP4/HLS serving tasks. Called only
/// after successful authorization, with the vault-derived fetch plan.
pub trait ResourceFetcher: Send + Sync {
    fn fetch(
        &self,
        kind: RouteKind,
        plan: FetchPlan,
        request: FetchRequest,
    ) -> Pin<Box<dyn Future<Output = Result<FetchedMedia, FetchError>> + Send>>;
}

/// Upstream fetch failure (ordinary failure, no privilege change).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FetchError {
    Upstream,
    NotFound,
}

/// Shared relay state.
pub struct RelayCore {
    pub registry: Mutex<SessionRegistry>,
    pub vault: Mutex<RecipeVault>,
    /// Logical clock supplier (ms); runtime injects a wall-clock adapter.
    pub now: Arc<dyn Fn() -> u64 + Send + Sync>,
    /// Per-process control secret for the loopback control plane.
    control_secret: Option<String>,
    fetcher: Option<Arc<dyn ResourceFetcher>>,
}

impl RelayCore {
    #[must_use]
    pub fn new(now: Arc<dyn Fn() -> u64 + Send + Sync>) -> Self {
        Self {
            registry: Mutex::new(SessionRegistry::new()),
            vault: Mutex::new(RecipeVault::new()),
            now,
            control_secret: None,
            fetcher: None,
        }
    }

    #[must_use]
    pub fn with_control_secret(mut self, secret: String) -> Self {
        self.control_secret = Some(secret);
        self
    }

    #[must_use]
    pub fn with_fetcher(mut self, fetcher: Arc<dyn ResourceFetcher>) -> Self {
        self.fetcher = Some(fetcher);
        self
    }
}

/// Loopback control router.
pub fn control_router(core: Arc<RelayCore>) -> Router {
    Router::new()
        .route("/internal/cast/session", post(create_session))
        .route("/internal/cast/session/{token}", delete(stop_session))
        .route("/internal/health", get(health))
        .layer(axum::extract::DefaultBodyLimit::max(MAX_CONTROL_BODY_BYTES))
        .with_state(core)
}

/// LAN media router: opaque session/resource routes only.
pub fn media_router(core: Arc<RelayCore>) -> Router {
    Router::new()
        .route("/s/{token}/master.m3u8", get(serve_master))
        .route("/s/{token}/manifest.mpd", get(serve_manifest))
        .route("/s/{token}/r/{resource_id}/{name}", get(serve_resource))
        .with_state(core)
}

/// `POST /internal/cast/session` request.
#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CreateSessionRequest {
    pub receiver_id: String,
    pub upstream_allow_set: Vec<String>,
    pub ttl_ms: Option<u64>,
    /// Override the receiver IP binding; defaults to the peer address.
    pub receiver_ip: Option<std::net::IpAddr>,
}

/// Session creation response (token returned once, over loopback only).
#[derive(Debug, Serialize)]
pub struct CreateSessionResponse {
    pub session_id: String,
    pub token: String,
}

fn check_control_secret(core: &RelayCore, headers: &HeaderMap) -> Option<Response> {
    let Some(expected) = &core.control_secret else {
        return None;
    };
    let presented = headers
        .get(CONTROL_SECRET_HEADER)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    // 定长比较：secret 长度一致时逐字节异或
    let ok = presented.len() == expected.len()
        && presented
            .as_bytes()
            .iter()
            .zip(expected.as_bytes())
            .fold(0u8, |acc, (a, b)| acc | (a ^ b))
            == 0;
    if ok {
        None
    } else {
        Some(error_response(
            StatusCode::UNAUTHORIZED,
            "control_unauthorized",
        ))
    }
}

async fn health() -> &'static str {
    "ok"
}

async fn create_session(
    State(core): State<Arc<RelayCore>>,
    headers: HeaderMap,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    Json(request): Json<CreateSessionRequest>,
) -> Response {
    if let Some(deny) = check_control_secret(&core, &headers) {
        return deny;
    }
    let Ok(receiver) = DeviceId::new(&request.receiver_id) else {
        return error_response(StatusCode::BAD_REQUEST, "invalid_receiver");
    };
    if request.upstream_allow_set.is_empty() || request.upstream_allow_set.len() > 64 {
        return error_response(StatusCode::BAD_REQUEST, "invalid_allow_set");
    }
    let now = (core.now)();
    let grant = core.registry.lock().unwrap().create_session(
        receiver,
        Some(request.receiver_ip.unwrap_or(peer.ip())),
        request.upstream_allow_set,
        request
            .ttl_ms
            .unwrap_or(crate::session::DEFAULT_SESSION_TTL_MS),
        now,
    );
    match grant {
        Some(grant) => Json(CreateSessionResponse {
            session_id: grant.session_id.to_string(),
            token: grant.token_hex,
        })
        .into_response(),
        None => error_response(StatusCode::SERVICE_UNAVAILABLE, "capacity_exceeded"),
    }
}

async fn stop_session(
    State(core): State<Arc<RelayCore>>,
    headers: HeaderMap,
    Path(token): Path<String>,
) -> Response {
    if let Some(deny) = check_control_secret(&core, &headers) {
        return deny;
    }
    let stopped = core.registry.lock().unwrap().stop(&token);
    let _ = stopped; // 幂等：已停止同样 204
    StatusCode::NO_CONTENT.into_response()
}

async fn serve_master(
    state: State<Arc<RelayCore>>,
    peer: ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    path: Path<String>,
) -> Response {
    serve(state, peer, headers, path, RouteKind::MasterPlaylist).await
}

async fn serve_manifest(
    state: State<Arc<RelayCore>>,
    peer: ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    path: Path<String>,
) -> Response {
    serve(state, peer, headers, path, RouteKind::DashManifest).await
}

async fn serve(
    State(core): State<Arc<RelayCore>>,
    peer: ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Path(token): Path<String>,
    kind: RouteKind,
) -> Response {
    // master.m3u8 / manifest.mpd 映射到会话内的约定资源
    let resource = match kind {
        RouteKind::MasterPlaylist => ResourceId::new("master").ok(),
        RouteKind::DashManifest => ResourceId::new("manifest").ok(),
        RouteKind::Resource => None,
    };
    let Some(resource) = resource else {
        return error_response(StatusCode::BAD_REQUEST, "invalid_resource");
    };
    let request = FetchRequest {
        method: "GET".to_string(),
        range: headers
            .get(header::RANGE)
            .and_then(|v| v.to_str().ok())
            .map(str::to_string),
        if_none_match: headers
            .get(header::IF_NONE_MATCH)
            .and_then(|v| v.to_str().ok())
            .map(str::to_string),
    };
    serve_authorized(&core, &peer, &token, kind, resource, request).await
}

async fn serve_resource(
    State(core): State<Arc<RelayCore>>,
    peer: ConnectInfo<SocketAddr>,
    method: axum::http::Method,
    headers: HeaderMap,
    Path((token, resource_id, _name)): Path<(String, String, String)>,
) -> Response {
    let Some(resource) = ResourceId::new(&resource_id).ok() else {
        return error_response(StatusCode::BAD_REQUEST, "invalid_resource");
    };
    let request = FetchRequest {
        method: method.as_str().to_string(),
        range: headers
            .get(header::RANGE)
            .and_then(|v| v.to_str().ok())
            .map(str::to_string),
        if_none_match: headers
            .get(header::IF_NONE_MATCH)
            .and_then(|v| v.to_str().ok())
            .map(str::to_string),
    };
    serve_authorized(&core, &peer, &token, RouteKind::Resource, resource, request).await
}

async fn serve_authorized(
    core: &Arc<RelayCore>,
    peer: &ConnectInfo<SocketAddr>,
    token: &str,
    kind: RouteKind,
    resource: ResourceId,
    request: FetchRequest,
) -> Response {
    // RL-003：授权先于任何 upstream 访问。
    let access = {
        let registry = core.registry.lock().unwrap();
        match registry.authorize(token, &resource, Some(peer.0.ip()), (core.now)()) {
            Ok(access) => access,
            Err(SessionAuthError::UnknownSession | SessionAuthError::SessionExpired) => {
                return error_response(StatusCode::UNAUTHORIZED, "session_unknown")
            }
            Err(SessionAuthError::ReceiverMismatch) => {
                return error_response(StatusCode::FORBIDDEN, "receiver_mismatch")
            }
            Err(SessionAuthError::UnknownResource) => {
                return error_response(StatusCode::NOT_FOUND, "unknown_resource")
            }
        }
    };

    // 在 vault 锁内只构造不可变的 FetchPlan，锁外执行网络 IO。
    let plan = {
        let vault = core.vault.lock().unwrap();
        vault
            .get(&access.session_id, &resource)
            .map(|recipe| FetchPlan {
                url: recipe.url_for_upstream().to_string(),
                headers: recipe
                    .scoped_headers()
                    .into_iter()
                    .map(|(k, v)| (k.to_string(), v.to_string()))
                    .collect(),
                allow_set: access.upstream_allow_set.clone(),
                token_hex: token.to_string(),
                session_id: access.session_id.clone(),
                depth: access.resource.depth,
            })
    };
    let Some(plan) = plan else {
        return error_response(StatusCode::NOT_FOUND, "unknown_resource");
    };

    let Some(fetcher) = &core.fetcher else {
        return error_response(StatusCode::SERVICE_UNAVAILABLE, "serving_unavailable");
    };
    match fetcher.fetch(kind, plan, request).await {
        Ok(media) => {
            let mut response = Response::new(media.body);
            *response.status_mut() = StatusCode::from_u16(media.status).unwrap_or(StatusCode::OK);
            for (name, value) in media.headers {
                if let (Ok(name), Ok(value)) = (
                    name.parse::<header::HeaderName>(),
                    value.parse::<header::HeaderValue>(),
                ) {
                    response.headers_mut().insert(name, value);
                }
            }
            response
        }
        Err(FetchError::NotFound) => error_response(StatusCode::NOT_FOUND, "upstream_not_found"),
        Err(FetchError::Upstream) => error_response(StatusCode::BAD_GATEWAY, "upstream_rejected"),
    }
}

fn error_response(status: StatusCode, code: &'static str) -> Response {
    (status, Json(serde_json::json!({ "error": code }))).into_response()
}
