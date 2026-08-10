//! HLS serving (MED-15): master/media playlist fetch + opaque rewrite,
//! binary segment passthrough, bounded playlist cache with ETag/304 and
//! live TTL refresh.
//!
//! - playlists are read bounded (256 KB cap) and rewritten via the MED-14
//!   parser; every referenced asset becomes a registered opaque resource
//!   (child recipes inherit the parent's scoped headers);
//! - segments/init maps stream byte-exact (RL-011: no text conversion);
//! - playlist cache: ≤64 entries, live TTL from EXT-X-TARGETDURATION
//!   (clamped 1..=30s), VOD/master 60s; `If-None-Match` against the cached
//!   ETag answers 304 without touching upstream; expired entries refetch
//!   (live playlist updates propagate);
//! - time comes from the runtime clock (RelayCore), never the wall clock.

use crate::hls::parser::{parse, rewrite, MAX_DEPTH};
use crate::mp4::{forward_headers, stream_body};
use crate::network_guard::NetworkGuard;
use crate::router::{
    FetchError, FetchPlan, FetchRequest, FetchedMedia, RelayCore, ResourceFetcher, RouteKind,
};
use crate::session::{generate_resource_id, SessionRegistry};
use crate::vault::{RecipeVault, UpstreamRecipe};
use axum::body::Body;
use futures_util::StreamExt;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex, OnceLock, Weak};
use std::time::Duration;

/// Playlist read cap.
const MAX_PLAYLIST_BYTES: usize = 256 * 1024;
/// Playlist cache bound.
const MAX_CACHE_ENTRIES: usize = 64;
/// VOD/master playlist cache TTL.
const STATIC_PLAYLIST_TTL_MS: u64 = 60_000;
/// Live playlist TTL bounds (from EXT-X-TARGETDURATION).
const LIVE_TTL_MIN_MS: u64 = 1_000;
const LIVE_TTL_MAX_MS: u64 = 30_000;
/// Read-idle timeout for upstream bodies.
const READ_IDLE: Duration = Duration::from_secs(15);

struct CacheEntry {
    token: String,
    url: String,
    etag: Option<String>,
    body: Vec<u8>,
    expires_ms: u64,
}

type Cache = Arc<Mutex<Vec<CacheEntry>>>;

fn cache_get(cache: &Cache, token: &str, url: &str, now: u64) -> Option<CacheHit> {
    let cache = cache.lock().unwrap();
    cache
        .iter()
        .find(|e| e.token == token && e.url == url && now <= e.expires_ms)
        .map(|e| CacheHit {
            etag: e.etag.clone(),
            body: e.body.clone(),
        })
}

struct CacheHit {
    etag: Option<String>,
    body: Vec<u8>,
}

/// Stale lookup for revalidation: returns the entry even after expiry (the
/// ETag stays useful for If-None-Match; the body is only served when fresh).
fn cache_get_stale(cache: &Cache, token: &str, url: &str) -> Option<CacheHit> {
    let cache = cache.lock().unwrap();
    cache
        .iter()
        .find(|e| e.token == token && e.url == url)
        .map(|e| CacheHit {
            etag: e.etag.clone(),
            body: e.body.clone(),
        })
}

fn cache_put(
    cache: &Cache,
    token: &str,
    url: &str,
    etag: Option<String>,
    body: Vec<u8>,
    now: u64,
    ttl_ms: u64,
) {
    let mut cache = cache.lock().unwrap();
    if cache.len() >= MAX_CACHE_ENTRIES {
        cache.retain(|e| now <= e.expires_ms);
        if cache.len() >= MAX_CACHE_ENTRIES {
            cache.sort_by_key(|e| e.expires_ms);
            cache.remove(0);
        }
    }
    cache.retain(|e| !(e.token == token && e.url == url));
    cache.push(CacheEntry {
        token: token.to_string(),
        url: url.to_string(),
        etag,
        body,
        expires_ms: now.saturating_add(ttl_ms),
    });
}

/// HLS resource fetcher (master/media playlists + binary segments).
pub struct HlsFetcher {
    guard: NetworkGuard,
    core: OnceLock<Weak<RelayCore>>,
    cache: Cache,
}

impl HlsFetcher {
    #[must_use]
    pub fn new(guard: NetworkGuard) -> Self {
        Self {
            guard,
            core: OnceLock::new(),
            cache: Arc::new(Mutex::new(Vec::new())),
        }
    }

    /// Binds the owning `RelayCore` (registry/vault/clock) after assembly.
    pub fn bind(&self, core: &Arc<RelayCore>) {
        let _ = self.core.set(Arc::downgrade(core));
    }
}

impl ResourceFetcher for HlsFetcher {
    fn fetch(
        &self,
        kind: RouteKind,
        plan: FetchPlan,
        request: FetchRequest,
    ) -> Pin<Box<dyn Future<Output = Result<FetchedMedia, FetchError>> + Send>> {
        let guard = self.guard.clone();
        let core = self.core.get().and_then(Weak::upgrade);
        let cache = self.cache.clone();
        let result = async move {
            match kind {
                RouteKind::MasterPlaylist | RouteKind::Resource => {}
                RouteKind::DashManifest => return Err(FetchError::NotFound),
            }
            let is_head = request.method == "HEAD";
            let now = core.as_ref().map(|c| (c.now)());

            // 缓存命中：ETag 一致 → 304；否则直接服务缓存 body。
            if !is_head {
                if let Some(now) = now {
                    if let Some(hit) = cache_get(&cache, &plan.token_hex, &plan.url, now) {
                        if let (Some(etag), Some(inm)) = (&hit.etag, &request.if_none_match) {
                            if etag == inm {
                                return Ok(FetchedMedia {
                                    status: 304,
                                    headers: vec![("etag".to_string(), etag.clone())],
                                    body: Body::empty(),
                                });
                            }
                        }
                        return Ok(FetchedMedia {
                            status: 200,
                            headers: playlist_headers(hit.etag.as_deref()),
                            body: Body::from(hit.body),
                        });
                    }
                }
            }

            // 上游拉取；带上缓存 ETag（含过期条目）以便上游 304 续约。
            let mut headers = plan.headers.clone();
            if let Some(range) = &request.range {
                headers.push(("Range".to_string(), range.clone()));
            }
            if now.is_some() {
                if let Some(hit) = cache_get_stale(&cache, &plan.token_hex, &plan.url) {
                    if let Some(etag) = hit.etag {
                        headers.push(("If-None-Match".to_string(), etag));
                    }
                }
            }
            let fetch = guard
                .fetch_with(
                    if is_head {
                        reqwest::Method::HEAD
                    } else {
                        reqwest::Method::GET
                    },
                    &plan.url,
                    &headers,
                    &plan.allow_set,
                )
                .await
                .map_err(|_| FetchError::Upstream)?;
            let response = fetch.response;
            let status = response.status().as_u16();
            if status == 304 {
                // 上游确认未变：续约缓存并按客户端条件头回 304 或 200。
                if let Some(now) = now {
                    if let Some(hit) = cache_get_stale(&cache, &plan.token_hex, &plan.url) {
                        let ttl = playlist_ttl(&String::from_utf8_lossy(&hit.body));
                        cache_put(
                            &cache,
                            &plan.token_hex,
                            &plan.url,
                            hit.etag.clone(),
                            hit.body.clone(),
                            now,
                            ttl,
                        );
                        if let (Some(etag), Some(inm)) = (&hit.etag, &request.if_none_match) {
                            if etag == inm {
                                return Ok(FetchedMedia {
                                    status: 304,
                                    headers: vec![("etag".to_string(), etag.clone())],
                                    body: Body::empty(),
                                });
                            }
                        }
                        return Ok(FetchedMedia {
                            status: 200,
                            headers: playlist_headers(hit.etag.as_deref()),
                            body: Body::from(hit.body),
                        });
                    }
                }
                return Err(FetchError::Upstream); // 无缓存却收到 304：异常，按普通失败
            }
            if status == 404 {
                return Err(FetchError::NotFound);
            }
            if !(200..300).contains(&status) {
                return Err(FetchError::Upstream);
            }

            let is_playlist = response
                .headers()
                .get(reqwest::header::CONTENT_TYPE)
                .and_then(|v| v.to_str().ok())
                .is_some_and(|ct| ct.contains("mpegurl"))
                || plan.url.split('?').next().unwrap_or("").ends_with(".m3u8");

            if !is_playlist {
                // 二进制分片/init：字节直通（RL-011）。
                let out_headers = forward_headers(&response);
                let body = if is_head {
                    Body::empty()
                } else {
                    stream_body(response, READ_IDLE)
                };
                return Ok(FetchedMedia {
                    status,
                    headers: out_headers,
                    body,
                });
            }

            // 播放列表：有界读取 → 解析改写 → 注册子资源。
            let etag = response
                .headers()
                .get(reqwest::header::ETAG)
                .and_then(|v| v.to_str().ok())
                .map(str::to_string);
            let body_bytes = read_capped(response, MAX_PLAYLIST_BYTES)
                .await
                .map_err(|_| FetchError::Upstream)?;
            let text = String::from_utf8_lossy(&body_bytes);
            let parsed = parse(&text).map_err(|_| FetchError::Upstream)?;
            if plan.depth >= MAX_DEPTH {
                return Err(FetchError::Upstream);
            }
            let core = core.ok_or(FetchError::Upstream)?;
            // 锁序：registry → vault（唯一双锁点，不得反向；锁内无 IO）。
            let rewritten = {
                let mut registry = core.registry.lock().unwrap();
                let mut vault = core.vault.lock().unwrap();
                rewrite(&parsed, &plan.url, plan.depth, |absolute| {
                    register_asset(&mut registry, &mut vault, &plan, absolute)
                })
                .map_err(|_| FetchError::Upstream)?
            };

            if let Some(now) = now {
                let ttl = playlist_ttl(&text);
                cache_put(
                    &cache,
                    &plan.token_hex,
                    &plan.url,
                    etag.clone(),
                    rewritten.clone().into_bytes(),
                    now,
                    ttl,
                );
            }
            Ok(FetchedMedia {
                status: 200,
                headers: playlist_headers(etag.as_deref()),
                body: if is_head {
                    Body::empty()
                } else {
                    Body::from(rewritten.into_bytes())
                },
            })
        };
        Box::pin(result)
    }
}

/// Registers one referenced asset as an opaque resource; the child recipe
/// inherits the parent's scoped headers.
fn register_asset(
    registry: &mut SessionRegistry,
    vault: &mut RecipeVault,
    plan: &FetchPlan,
    absolute: &str,
) -> Result<String, crate::hls::parser::HlsError> {
    use crate::hls::parser::HlsError;
    let parsed_url =
        url::Url::parse(absolute).map_err(|_| HlsError::AllocationFailed(absolute.to_string()))?;
    let host = parsed_url
        .host_str()
        .ok_or_else(|| HlsError::AllocationFailed(absolute.to_string()))?;
    let id = generate_resource_id();
    registry
        .register_resource(&plan.token_hex, id.clone(), host, plan.depth + 1)
        .map_err(|_| HlsError::AllocationFailed(absolute.to_string()))?;
    let (referer, ua) = scoped_header_values(&plan.headers);
    vault
        .store(
            &plan.session_id,
            id.clone(),
            UpstreamRecipe::new(absolute, referer, ua)
                .map_err(|_| HlsError::AllocationFailed(absolute.to_string()))?,
        )
        .map_err(|_| HlsError::AllocationFailed(absolute.to_string()))?;
    Ok(format!(
        "/s/{}/r/{}/{}",
        plan.token_hex,
        id,
        decorative_name(absolute)
    ))
}

fn scoped_header_values(headers: &[(String, String)]) -> (Option<String>, Option<String>) {
    let mut referer = None;
    let mut ua = None;
    for (name, value) in headers {
        if name.eq_ignore_ascii_case("referer") {
            referer = Some(value.clone());
        } else if name.eq_ignore_ascii_case("user-agent") {
            ua = Some(value.clone());
        }
    }
    (referer, ua)
}

/// Decorative filename for the opaque route (no query).
fn decorative_name(url: &str) -> String {
    url::Url::parse(url)
        .ok()
        .and_then(|u| {
            u.path_segments()
                .and_then(|mut s| s.next_back())
                .map(str::to_string)
        })
        .filter(|name| !name.is_empty())
        .unwrap_or_else(|| "asset".to_string())
}

fn playlist_headers(etag: Option<&str>) -> Vec<(String, String)> {
    let mut headers = vec![
        (
            "content-type".to_string(),
            "application/vnd.apple.mpegurl".to_string(),
        ),
        // 接收端必须回源验证：播放列表不做客户端缓存
        ("cache-control".to_string(), "no-cache".to_string()),
    ];
    if let Some(etag) = etag {
        headers.push(("etag".to_string(), etag.to_string()));
    }
    headers
}

/// Live TTL from EXT-X-TARGETDURATION (clamped); VOD/master get the static TTL.
fn playlist_ttl(text: &str) -> u64 {
    if text.contains("#EXT-X-ENDLIST") {
        return STATIC_PLAYLIST_TTL_MS;
    }
    text.lines()
        .find_map(|line| {
            line.trim()
                .strip_prefix("#EXT-X-TARGETDURATION:")
                .and_then(|v| v.parse::<u64>().ok())
        })
        .map(|secs| (secs * 1000).clamp(LIVE_TTL_MIN_MS, LIVE_TTL_MAX_MS))
        .unwrap_or(STATIC_PLAYLIST_TTL_MS)
}

/// Reads a response body fully, capped (playlist path only).
async fn read_capped(response: reqwest::Response, cap: usize) -> Result<Vec<u8>, ()> {
    let mut body = Vec::new();
    let mut stream = response.bytes_stream();
    while let Some(chunk) = stream.next().await {
        let chunk = chunk.map_err(|_| ())?;
        if body.len() + chunk.len() > cap {
            return Err(());
        }
        body.extend_from_slice(&chunk);
    }
    Ok(body)
}
