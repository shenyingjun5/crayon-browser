//! relay 代理核心：m3u8 重写、Range 透传、防盗链伪造、响应头净化、SSRF 防护。

use axum::{
    body::{Body, Bytes},
    extract::{Query, Request, State},
    http::{header, HeaderMap, HeaderValue, StatusCode},
    response::{IntoResponse, Response},
};
use futures_util::StreamExt;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

/// m3u8 递归重写最大深度（docs/design.md §5）。
pub const MAX_M3U8_DEPTH: u8 = 5;
/// 判定/读取 m3u8 内容时的 body 上限（播放列表都很小）。
const MAX_PLAYLIST_BYTES: usize = 16 * 1024 * 1024;
const UPSTREAM_TIMEOUT: Duration = Duration::from_secs(10);
/// 失败重试次数（不含首次），docs/design.md §5：超时 10s 重试 2 次。
const MAX_RETRIES: usize = 2;

pub struct ProxyState {
    pub client: reqwest::Client,
    /// 测试钩子：允许代理内网/本机地址（默认 false，SSRF 黑名单生效）。
    pub allow_private_hosts: bool,
}

impl ProxyState {
    pub fn new(allow_private_hosts: bool) -> Arc<Self> {
        let client = reqwest::Client::builder()
            .user_agent(crate::DEFAULT_UA)
            .connect_timeout(UPSTREAM_TIMEOUT)
            // 不用总超时（会掐断大文件/慢速分片流）；读空闲超时兜底
            .read_timeout(UPSTREAM_TIMEOUT)
            .redirect(reqwest::redirect::Policy::limited(5))
            .build()
            .expect("reqwest client build");
        Arc::new(Self {
            client,
            allow_private_hosts,
        })
    }
}

/// `GET /proxy/<urlencoded目标URL>[/<装饰性文件名>]?referer=...&ua=...&depth=N`
///
/// 注意：不用 axum 的 `Path<String>`（它会预先 percent-decode，导致双重解码），
/// 直接从原始 URI path 取 `/proxy/` 之后的内容。
/// 编码目标本身不含裸 `/`，因此第一个 `/` 之后是可选的装饰性文件名后缀——
/// 部分播放器（如打了 CVE-2023-6604 扩展名严格校验补丁的 ffmpeg）要求 URL
/// 以真实扩展名结尾，重写时会自动追加该后缀；这里解析时忽略它。
pub async fn proxy_handler(
    State(state): State<crate::relay::AppState>,
    Query(params): Query<HashMap<String, String>>,
    req: Request,
) -> Response {
    let path = req.uri().path();
    let rest = path.strip_prefix("/proxy/").unwrap_or("");
    let encoded = rest.split('/').next().unwrap_or("").to_string();
    let host_header = req
        .headers()
        .get(header::HOST)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("127.0.0.1:8321")
        .to_string();
    let client_range = req
        .headers()
        .get(header::RANGE)
        .and_then(|v| v.to_str().ok())
        .map(|s| s.to_string());

    // S3：畸形 percent 编码 → 400
    if !valid_percent_encoding(&encoded) {
        return plain(StatusCode::BAD_REQUEST, "invalid percent-encoding");
    }
    let target = match percent_encoding::percent_decode_str(&encoded).decode_utf8() {
        Ok(t) => t.into_owned(),
        Err(_) => return plain(StatusCode::BAD_REQUEST, "invalid url encoding"),
    };

    // S2：仅 http/https；S1：SSRF 黑名单
    let parsed = match url::Url::parse(&target) {
        Ok(u) => u,
        Err(_) => return plain(StatusCode::BAD_REQUEST, "invalid target url"),
    };
    if parsed.scheme() != "http" && parsed.scheme() != "https" {
        return plain(StatusCode::BAD_REQUEST, "only http/https allowed");
    }
    if !state.proxy.allow_private_hosts && is_blocked_host(&parsed) {
        return plain(StatusCode::BAD_REQUEST, "blocked host (SSRF protection)");
    }

    // R6：递归深度限制
    let depth: u8 = params
        .get("depth")
        .and_then(|d| d.parse().ok())
        .unwrap_or(0);
    if depth > MAX_M3U8_DEPTH {
        return plain(StatusCode::BAD_REQUEST, "m3u8 recursion depth exceeded");
    }

    let referer = params.get("referer").cloned();
    let ua = params.get("ua").cloned();

    // 防盗链伪造：UA/Referer 按参数设置，缺省取目标 origin；不转发客户端 Cookie
    let mut upstream_headers = HeaderMap::new();
    upstream_headers.insert(
        header::USER_AGENT,
        HeaderValue::from_str(ua.as_deref().unwrap_or(crate::DEFAULT_UA))
            .unwrap_or_else(|_| HeaderValue::from_static("get-video")),
    );
    let referer_value = referer
        .clone()
        .unwrap_or_else(|| parsed.origin().ascii_serialization());
    if let Ok(v) = HeaderValue::from_str(&referer_value) {
        upstream_headers.insert(header::REFERER, v);
    }
    // Range 透传：仅对非播放列表内容转发。m3u8 要被重写，字节会变，
    // 若上游按 Range 返回 206 + Content-Range（基于重写前的长度），客户端会
    // 按旧长度截断正文导致解析失败——播放列表一律全量拉取。
    let target_is_m3u8 = parsed.path().to_ascii_lowercase().ends_with(".m3u8");
    if !target_is_m3u8 {
        if let Some(r) = &client_range {
            if let Ok(v) = HeaderValue::from_str(r) {
                upstream_headers.insert(header::RANGE, v);
            }
        }
    }

    // 超时 10s / 重试 2 次
    let mut last_err = String::new();
    let mut upstream = None;
    for attempt in 0..=MAX_RETRIES {
        let mut builder = state.proxy.client.get(&target);
        builder = builder.headers(upstream_headers.clone());
        match builder.send().await {
            Ok(resp) => {
                upstream = Some(resp);
                break;
            }
            Err(e) => {
                last_err = format!("{e}");
                tracing::warn!(target, attempt, "upstream request failed: {last_err}");
            }
        }
    }
    let Some(resp) = upstream else {
        return plain(
            StatusCode::BAD_GATEWAY,
            &format!("upstream request failed: {last_err}"),
        );
    };

    let status =
        StatusCode::from_u16(resp.status().as_u16()).unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
    if !status.is_success() {
        tracing::warn!(target, %status, range=?client_range, "relay upstream non-2xx");
    }
    let resp_headers = resp.headers().clone();
    let ct = resp_headers
        .get(header::CONTENT_TYPE)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("")
        .to_ascii_lowercase();
    let ct_m3u8 = ct.contains("mpegurl") || ct.contains("m3u8");

    if ct_m3u8 {
        // Content-Type 声明为 m3u8：读全量（上限 16MB）后按内容确认再重写
        let body = match read_body_bounded(resp, MAX_PLAYLIST_BYTES).await {
            Ok(b) => b,
            Err(e) => return plain(StatusCode::BAD_GATEWAY, &format!("read upstream body: {e}")),
        };
        if looks_like_m3u8(&body) {
            let text = String::from_utf8_lossy(&body).into_owned();
            let rewritten = rewrite_m3u8(
                &text,
                &target,
                &host_header,
                referer.as_deref(),
                ua.as_deref(),
                depth,
            );
            return build_response(
                status,
                &resp_headers,
                Body::from(rewritten),
                Some("application/vnd.apple.mpegurl"),
                true,
            );
        }
        return build_response(status, &resp_headers, Body::from(body), None, false);
    }

    // Content-Type 未声明 m3u8：peek 首块，按内容判定（R7）
    let mut stream = resp.bytes_stream();
    let mut buf: Vec<u8> = Vec::new();
    let mut upstream_failed = None;
    while buf.len() < 16 {
        match stream.next().await {
            Some(Ok(chunk)) => {
                buf.extend_from_slice(&chunk);
                if buf.len() >= 16 {
                    break;
                }
            }
            Some(Err(e)) => {
                upstream_failed = Some(format!("{e}"));
                break;
            }
            None => break,
        }
    }
    if let Some(e) = upstream_failed {
        if buf.is_empty() {
            return plain(
                StatusCode::BAD_GATEWAY,
                &format!("upstream stream error: {e}"),
            );
        }
    }

    if looks_like_m3u8(&buf) {
        // 内容确认为 m3u8：继续读完（有界），整体重写
        let mut body = buf;
        while let Some(chunk) = stream.next().await {
            match chunk {
                Ok(c) => {
                    body.extend_from_slice(&c);
                    if body.len() > MAX_PLAYLIST_BYTES {
                        return plain(StatusCode::BAD_GATEWAY, "playlist too large");
                    }
                }
                Err(e) => {
                    return plain(
                        StatusCode::BAD_GATEWAY,
                        &format!("upstream stream error: {e}"),
                    );
                }
            }
        }
        let text = String::from_utf8_lossy(&body).into_owned();
        let rewritten = rewrite_m3u8(
            &text,
            &target,
            &host_header,
            referer.as_deref(),
            ua.as_deref(),
            depth,
        );
        return build_response(
            status,
            &resp_headers,
            Body::from(rewritten),
            Some("application/vnd.apple.mpegurl"),
            true,
        );
    }

    // 真流式转发：先吐已缓冲的首块，再链上后续流，不全量入内存（R12）
    let first: Result<Bytes, std::io::Error> = Ok(Bytes::from(buf));
    let rest = stream.map(|r| r.map_err(std::io::Error::other));
    let body = Body::from_stream(futures_util::stream::once(async move { first }).chain(rest));
    build_response(status, &resp_headers, body, None, false)
}

/// OPTIONS 预检（R16）。
pub async fn options_handler() -> Response {
    let mut resp = plain(StatusCode::NO_CONTENT, "");
    let h = resp.headers_mut();
    h.insert(
        header::ACCESS_CONTROL_ALLOW_METHODS,
        HeaderValue::from_static("GET, OPTIONS"),
    );
    h.insert(
        header::ACCESS_CONTROL_ALLOW_HEADERS,
        HeaderValue::from_static("*"),
    );
    h.insert(
        header::ACCESS_CONTROL_MAX_AGE,
        HeaderValue::from_static("86400"),
    );
    resp
}

/// 构造转发响应：状态/Range 相关头原样回传，敏感头净化，统一 CORS。
fn build_response(
    status: StatusCode,
    upstream_headers: &HeaderMap,
    body: Body,
    override_content_type: Option<&'static str>,
    is_playlist: bool,
) -> Response {
    let mut resp = Response::new(body);
    // 播放列表经重写后是一个全新的完整响应，状态统一为 200
    // （上游可能因客户端 Range 返回 206，与重写后的正文不匹配）
    *resp.status_mut() = if is_playlist { StatusCode::OK } else { status };
    let out = resp.headers_mut();

    // 透传白名单：Range 相关（R9/R10）、缓存与类型
    for name in [
        header::CONTENT_RANGE,
        header::ACCEPT_RANGES,
        header::CONTENT_LENGTH,
        header::CACHE_CONTROL,
        header::ETAG,
        header::LAST_MODIFIED,
    ] {
        if let Some(v) = upstream_headers.get(&name) {
            out.insert(name, v.clone());
        }
    }
    if let Some(ct) = override_content_type {
        out.insert(header::CONTENT_TYPE, HeaderValue::from_static(ct));
    } else if let Some(v) = upstream_headers.get(header::CONTENT_TYPE) {
        out.insert(header::CONTENT_TYPE, v.clone());
    }
    if is_playlist {
        out.remove(header::CONTENT_LENGTH); // 重写后长度变化
        out.remove(header::CONTENT_RANGE); // 重写后 Content-Range 不再有效
        out.remove(header::ACCEPT_RANGES);
    }
    // 敏感头净化：content-security-policy / set-cookie / x-frame-options（白名单之外的天然不带，
    // 这里显式删除兜底）
    out.remove(header::SET_COOKIE);
    out.remove("content-security-policy");
    out.remove(header::X_FRAME_OPTIONS);
    // 统一 CORS
    out.insert(
        header::ACCESS_CONTROL_ALLOW_ORIGIN,
        HeaderValue::from_static("*"),
    );
    resp
}

fn plain(status: StatusCode, msg: &str) -> Response {
    let mut resp = (status, msg.to_string()).into_response();
    resp.headers_mut().insert(
        header::ACCESS_CONTROL_ALLOW_ORIGIN,
        HeaderValue::from_static("*"),
    );
    resp
}

fn looks_like_m3u8(bytes: &[u8]) -> bool {
    let b = match bytes.strip_prefix(b"\xef\xbb\xbf") {
        Some(rest) => rest,
        None => bytes,
    };
    let b = &b[..b.len().min(64)];
    let text = String::from_utf8_lossy(b);
    text.trim_start().starts_with("#EXTM3U")
}

async fn read_body_bounded(resp: reqwest::Response, limit: usize) -> Result<Vec<u8>, String> {
    let mut stream = resp.bytes_stream();
    let mut body = Vec::new();
    while let Some(chunk) = stream.next().await {
        let c = chunk.map_err(|e| format!("{e}"))?;
        body.extend_from_slice(&c);
        if body.len() > limit {
            return Err("body too large".into());
        }
    }
    Ok(body)
}

/// m3u8 逐行重写：分片行、EXT-X-KEY/EXT-X-MAP/EXT-X-MEDIA 的 URI 转绝对后改写回 /proxy/。
/// master 保留多码率结构，不自动选档。
pub fn rewrite_m3u8(
    text: &str,
    base_url: &str,
    relay_host: &str,
    referer: Option<&str>,
    ua: Option<&str>,
    depth: u8,
) -> String {
    let mut out = String::with_capacity(text.len() + 256);
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.is_empty() {
            out.push('\n');
            continue;
        }
        if trimmed.starts_with("#EXT-X-KEY:")
            || trimmed.starts_with("#EXT-X-SESSION-KEY:")
            || trimmed.starts_with("#EXT-X-MAP:")
            || trimmed.starts_with("#EXT-X-MEDIA:")
        {
            out.push_str(&rewrite_uri_attrs(
                trimmed, base_url, relay_host, referer, ua, depth,
            ));
            out.push('\n');
            continue;
        }
        if trimmed.starts_with('#') {
            // 其它标签（含 EXT-X-BYTERANGE、EXT-X-STREAM-INF）原样保留
            out.push_str(trimmed);
            out.push('\n');
            continue;
        }
        // 分片/子列表行：转绝对 → 改写回 /proxy/
        if let Some(abs) = crate::extract::resolve_url(base_url, trimmed) {
            out.push_str(&build_proxy_url(&abs, relay_host, referer, ua, depth));
        } else {
            out.push_str(trimmed);
        }
        out.push('\n');
    }
    out
}

/// 改写标签行内的所有 URI="..." 属性。
fn rewrite_uri_attrs(
    line: &str,
    base_url: &str,
    relay_host: &str,
    referer: Option<&str>,
    ua: Option<&str>,
    depth: u8,
) -> String {
    let mut out = String::with_capacity(line.len() + 128);
    let mut rest = line;
    while let Some(idx) = rest.find("URI=\"") {
        let (before, after) = rest.split_at(idx);
        out.push_str(before);
        out.push_str("URI=\"");
        let after = &after[5..];
        match after.find('"') {
            Some(end) => {
                let uri = &after[..end];
                if let Some(abs) = crate::extract::resolve_url(base_url, uri) {
                    out.push_str(&build_proxy_url(&abs, relay_host, referer, ua, depth));
                } else {
                    out.push_str(uri);
                }
                out.push('"');
                rest = &after[end + 1..];
            }
            None => {
                out.push_str(after);
                rest = "";
            }
        }
    }
    out.push_str(rest);
    out
}

/// 拼本地代理地址；m3u8 链接递增递归深度。
/// 追加目标 URL 的文件名作装饰性路径后缀（`/proxy/<编码>/<文件名>`），
/// 让对 URL 扩展名挑剔的播放器（如打了 CVE-2023-6604 补丁的 ffmpeg）能通过校验。
fn build_proxy_url(
    abs_url: &str,
    relay_host: &str,
    referer: Option<&str>,
    ua: Option<&str>,
    depth: u8,
) -> String {
    let parsed = url::Url::parse(abs_url).ok();
    let is_m3u8 = parsed
        .as_ref()
        .map(|u| u.path().to_ascii_lowercase().ends_with(".m3u8"))
        .unwrap_or(false);
    let mut u = format!(
        "http://{}/proxy/{}",
        relay_host,
        crate::encode_url_component(abs_url)
    );
    // 装饰性文件名后缀：仅保留安全字符，取不到或为空则不加
    if let Some(p) = &parsed {
        if let Some(name) = p.path().rsplit('/').next() {
            if !name.is_empty()
                && name
                    .chars()
                    .all(|c| c.is_ascii_alphanumeric() || matches!(c, '.' | '_' | '-'))
            {
                u.push('/');
                u.push_str(name);
            }
        }
    }
    let mut qs = vec![];
    if let Some(r) = referer {
        qs.push(format!("referer={}", crate::encode_url_component(r)));
    }
    if let Some(uastr) = ua {
        qs.push(format!("ua={}", crate::encode_url_component(uastr)));
    }
    if is_m3u8 {
        qs.push(format!("depth={}", depth + 1));
    }
    if !qs.is_empty() {
        u.push('?');
        u.push_str(&qs.join("&"));
    }
    u
}

/// 校验 percent 编码合法性：每个 `%` 后必须跟两个十六进制字符（S3）。
fn valid_percent_encoding(s: &str) -> bool {
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' {
            if i + 2 >= bytes.len() {
                return false;
            }
            let h = bytes[i + 1];
            let l = bytes[i + 2];
            if !h.is_ascii_hexdigit() || !l.is_ascii_hexdigit() {
                return false;
            }
            i += 3;
        } else {
            i += 1;
        }
    }
    true
}

/// SSRF 黑名单（对标 LibreTV server.mjs isValidUrl）。
pub fn is_blocked_host(url: &url::Url) -> bool {
    let host = match url.host_str() {
        Some(h) => h.to_ascii_lowercase(),
        None => return true,
    };
    if host == "localhost" || host.ends_with(".localhost") || host == "0.0.0.0" {
        return true;
    }
    // IPv6 回环/链路本地
    if host == "[::1]" || host == "::1" || host.starts_with("[fe80") {
        return true;
    }
    let parts: Vec<&str> = host.split('.').collect();
    if parts.len() == 4 && parts.iter().all(|p| p.parse::<u8>().is_ok()) {
        let a: u8 = parts[0].parse().unwrap();
        let b: u8 = parts[1].parse().unwrap();
        // 127.* / 10.* / 192.168.* / 172.16-31.* / 169.254.* / 0.*
        if a == 127
            || a == 10
            || a == 0
            || (a == 192 && b == 168)
            || (a == 172 && (16..=31).contains(&b))
            || (a == 169 && b == 254)
        {
            return true;
        }
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ssrf_blocklist() {
        for u in [
            "http://127.0.0.1/x",
            "http://localhost/x",
            "http://10.0.0.1/x",
            "http://192.168.1.1/x",
            "http://172.16.0.1/x",
            "http://172.31.255.1/x",
            "http://169.254.169.254/latest/meta-data",
            "http://[::1]/x",
        ] {
            assert!(is_blocked_host(&url::Url::parse(u).unwrap()), "{u}");
        }
        for u in [
            "http://172.32.0.1/x",
            "https://example.com/x",
            "http://8.8.8.8/x",
        ] {
            assert!(!is_blocked_host(&url::Url::parse(u).unwrap()), "{u}");
        }
    }

    #[test]
    fn percent_encoding_validation() {
        assert!(valid_percent_encoding("https%3A%2F%2Fa.com%2Fx.m3u8"));
        assert!(valid_percent_encoding("plain"));
        assert!(!valid_percent_encoding("bad%2"));
        assert!(!valid_percent_encoding("bad%zz"));
        assert!(!valid_percent_encoding("100%"));
    }

    #[test]
    fn rewrite_basic() {
        let text = "#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:5.0,\nseg1.ts\n#EXT-X-ENDLIST\n";
        let out = rewrite_m3u8(
            text,
            "http://up.example.com/live/index.m3u8",
            "127.0.0.1:8321",
            Some("http://up.example.com/"),
            None,
            0,
        );
        assert!(out.contains("#EXTINF:5.0,"));
        assert!(out.contains(
            "http://127.0.0.1:8321/proxy/http%3A%2F%2Fup.example.com%2Flive%2Fseg1.ts/seg1.ts?referer=http%3A%2F%2Fup.example.com%2F"
        ));
    }
}
