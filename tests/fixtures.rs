//! 夹具测试（docs/test-cases.md）：
//! E2-E7（L1 静态解析）、R5-R8/R10/R12-R16（relay）、S1-S3（安全）、D1/D2/D4-夹具（DRM）。
//! 全部本地构造、确定性，CI 可跑。

use axum::{
    body::{Body, Bytes},
    extract::{Path, Query, Request, State},
    http::{header, HeaderMap, HeaderValue, StatusCode},
    response::{IntoResponse, Response},
    routing::get,
    Router,
};
use crayon_browser_core::extract::{parse_html, Extractor, Format, Protocol, RulePack};
use crayon_browser_core::relay::{self, RelayConfig};
use futures_util::StreamExt;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};

// ---------------------------------------------------------------------------
// 测试基础设施
// ---------------------------------------------------------------------------

struct UpstreamState {
    base: String,
}

/// mock 上游：覆盖校验 Referer/UA、敏感头、大 body、不支持 Range 等场景。
async fn spawn_upstream() -> String {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let base = format!("http://{}", listener.local_addr().unwrap());
    let state = Arc::new(UpstreamState { base: base.clone() });

    let app = Router::new()
        // R13：校验 Referer 的防盗链源
        .route("/guard/referer", get(guard_referer))
        // R14：校验 UA 的源
        .route("/guard/ua", get(guard_ua))
        // R15：敏感响应头
        .route("/sensitive", get(sensitive_headers))
        // R10：忽略 Range 的上游
        .route("/no-range", get(no_range))
        // 支持 Range 的 mp4（备用）
        .route("/video.mp4", get(range_mp4))
        // R12：50MB 大文件，分块慢吐
        .route("/big", get(big_stream))
        // R5-R8 等 m3u8 夹具统一走文件分发
        .route("/m3u8/{file}", get(m3u8_file))
        .route("/seg.ts", get(seg_ts))
        // D1/D2/D4 夹具：DRM / 非 DRM 播放列表与嵌入页面
        .route("/drm/fps.m3u8", get(drm_fps_playlist))
        .route("/drm/wv.m3u8", get(drm_wv_playlist))
        .route("/drm/clean.mpd", get(clean_mpd))
        .route("/page/drm_fps.html", get(drm_fps_page))
        .route("/page/drm_wv.html", get(drm_wv_page))
        .route("/page/clean_dash.html", get(clean_dash_page))
        // D5：流失效夹具——主列表 200 但变体 404（央视 4K 专区老片实测场景）
        .route("/dead/master.m3u8", get(dead_master_playlist))
        .route("/dead/variant.m3u8", get(dead_variant_404))
        .route("/page/dead_hls.html", get(dead_hls_page))
        // E8：央视站点解析器夹具（模拟 getHttpVideoInfo.do）
        .route("/cntv/getHttpVideoInfo.do", get(cntv_video_info))
        // E9：B 站番剧夹具（模拟 pgc/playurl；dash_only 版 durl 为空走 DASH 兜底）
        .route("/bili/pgc/playurl", get(bili_playurl))
        .route("/bili_dash/pgc/playurl", get(bili_playurl_dash_only))
        // E9c：B 站普通视频夹具（view 换 cid + ugc playurl）
        .route("/bili/x/web-interface/view", get(bili_view))
        .route("/bili/x/player/playurl", get(bili_ugc_playurl))
        .with_state(state);

    tokio::spawn(async move { axum::serve(listener, app).await.unwrap() });
    base
}

/// 启动 relay；`allow_private=true` 用于代理本机 mock 上游（测试钩子）。
async fn spawn_relay(allow_private: bool) -> relay::RelayHandle {
    relay::start(RelayConfig {
        host: "127.0.0.1".into(),
        port: 0,
        allow_private_hosts: allow_private,
        rules_path: None,
        dash_store: None,
    })
    .await
    .unwrap()
}

fn proxy_url(relay_base: &str, target: &str, extra_query: &str) -> String {
    let mut u = format!(
        "{}/proxy/{}",
        relay_base,
        crayon_browser_core::encode_url_component(target)
    );
    if !extra_query.is_empty() {
        u.push('?');
        u.push_str(extra_query);
    }
    u
}

/// 从 relay 重写后的 m3u8 文本中提取首个 /proxy/ 地址的目标 URL。
fn first_proxied_target(body: &str) -> Option<String> {
    for line in body.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some(idx) = line.find("/proxy/") {
            let rest = &line[idx + 7..];
            // 编码目标之后可能跟装饰性文件名后缀（/xxx.ts）和 query
            let enc = rest.split(['/', '?', '\r']).next().unwrap_or("");
            return Some(
                percent_encoding::percent_decode_str(enc)
                    .decode_utf8_lossy()
                    .into_owned(),
            );
        }
    }
    None
}

// ---------------------------------------------------------------------------
// mock 上游 handlers
// ---------------------------------------------------------------------------

async fn guard_referer(req: Request) -> Response {
    let ok = req
        .headers()
        .get(header::REFERER)
        .and_then(|v| v.to_str().ok())
        .map(|r| r == "http://allowed.example/")
        .unwrap_or(false);
    if ok {
        (StatusCode::OK, "guard-pass").into_response()
    } else {
        (StatusCode::FORBIDDEN, "guard-block").into_response()
    }
}

async fn guard_ua(req: Request) -> Response {
    let ua = req
        .headers()
        .get(header::USER_AGENT)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    if ua.starts_with("Mozilla/5.0") {
        (StatusCode::OK, "ua-pass").into_response()
    } else {
        (StatusCode::FORBIDDEN, "ua-block").into_response()
    }
}

async fn sensitive_headers() -> Response {
    let mut resp = (StatusCode::OK, "sensitive-body").into_response();
    let h = resp.headers_mut();
    h.insert(header::SET_COOKIE, HeaderValue::from_static("session=abc"));
    h.insert(header::X_FRAME_OPTIONS, HeaderValue::from_static("DENY"));
    h.insert(
        "content-security-policy",
        HeaderValue::from_static("default-src 'none'"),
    );
    resp
}

async fn no_range() -> Response {
    // 上游不理解 Range：无论客户端带什么都返回 200 全量
    (StatusCode::OK, "0123456789abcdef").into_response()
}

fn fake_mp4_bytes() -> Vec<u8> {
    // 不是真 mp4，仅验证 Range 语义
    (0..100_000u32).map(|i| (i % 251) as u8).collect()
}

async fn range_mp4(req: Request) -> Response {
    let data = fake_mp4_bytes();
    let range = req
        .headers()
        .get(header::RANGE)
        .and_then(|v| v.to_str().ok())
        .and_then(|r| r.strip_prefix("bytes="))
        .map(|s| s.to_string());
    let mut headers = HeaderMap::new();
    headers.insert(header::ACCEPT_RANGES, HeaderValue::from_static("bytes"));
    headers.insert(header::CONTENT_TYPE, HeaderValue::from_static("video/mp4"));
    if let Some(r) = range {
        let mut it = r.split('-');
        let start: usize = it.next().and_then(|s| s.parse().ok()).unwrap_or(0);
        let end: usize = it
            .next()
            .and_then(|s| s.parse().ok())
            .unwrap_or(data.len() - 1)
            .min(data.len() - 1);
        let slice = data[start..=end].to_vec();
        headers.insert(
            header::CONTENT_RANGE,
            HeaderValue::from_str(&format!("bytes {start}-{end}/{}", data.len())).unwrap(),
        );
        headers.insert(
            header::CONTENT_LENGTH,
            HeaderValue::from_str(&slice.len().to_string()).unwrap(),
        );
        return (StatusCode::PARTIAL_CONTENT, headers, slice).into_response();
    }
    headers.insert(
        header::CONTENT_LENGTH,
        HeaderValue::from_str(&data.len().to_string()).unwrap(),
    );
    (StatusCode::OK, headers, data).into_response()
}

async fn big_stream() -> Response {
    // 50MB，256KB 一块，每块 5ms：全量约 1s+
    let chunk_len = 256 * 1024usize;
    let total = 50 * 1024 * 1024usize;
    let stream = futures_util::stream::unfold(0usize, move |sent| async move {
        if sent >= total {
            return None;
        }
        tokio::time::sleep(Duration::from_millis(5)).await;
        let n = chunk_len.min(total - sent);
        Some((
            Ok::<Bytes, std::io::Error>(Bytes::from(vec![0xABu8; n])),
            sent + n,
        ))
    });
    let mut resp = Response::new(Body::from_stream(stream));
    resp.headers_mut().insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static("application/octet-stream"),
    );
    resp
}

/// m3u8 夹具分发：map.m3u8（R5）、chain0..6.m3u8（R6）、plain.m3u8（R7）、query.m3u8（R8）。
async fn m3u8_file(Path(file): Path<String>) -> Response {
    if let Some(n_str) = file
        .strip_prefix("chain")
        .and_then(|s| s.strip_suffix(".m3u8"))
    {
        if let Ok(n) = n_str.parse::<u32>() {
            let body = if n < 6 {
                format!(
                    "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360\nchain{}.m3u8\n",
                    n + 1
                )
            } else {
                "#EXTM3U\n#EXTINF:5.0,\n../seg.ts\n#EXT-X-ENDLIST\n".to_string()
            };
            return playlist_response_owned(body);
        }
    }
    match file.as_str() {
        "map.m3u8" => playlist_response(
            "#EXTM3U\n\
             #EXT-X-TARGETDURATION:6\n\
             #EXT-X-MAP:URI=\"init.mp4\"\n\
             #EXTINF:6.0,\n\
             seg1.m4s\n\
             #EXT-X-ENDLIST\n",
        ),
        "plain.m3u8" => {
            // Content-Type 为 text/plain 但内容是 m3u8（R7）
            let mut resp = (
                StatusCode::OK,
                "#EXTM3U\n#EXTINF:5.0,\n../seg.ts\n#EXT-X-ENDLIST\n",
            )
                .into_response();
            resp.headers_mut()
                .insert(header::CONTENT_TYPE, HeaderValue::from_static("text/plain"));
            resp
        }
        "query.m3u8" => {
            playlist_response("#EXTM3U\n#EXTINF:5.0,\n../seg.ts?token=abc&x=1\n#EXT-X-ENDLIST\n")
        }
        _ => (StatusCode::NOT_FOUND, "no such fixture").into_response(),
    }
}

async fn seg_ts(Query(q): Query<std::collections::HashMap<String, String>>) -> Response {
    // 回显 query，便于 R8 断言参数不丢失
    let mut keys: Vec<_> = q.keys().cloned().collect();
    keys.sort();
    let echo = keys
        .iter()
        .map(|k| format!("{k}={}", q[k]))
        .collect::<Vec<_>>()
        .join("&");
    (StatusCode::OK, format!("seg-bytes:{echo}")).into_response()
}

async fn drm_fps_playlist() -> Response {
    playlist_response(
        "#EXTM3U\n\
         #EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://fps.example/key\",KEYFORMAT=\"com.apple.streamingkeydelivery\",KEYFORMATVERSIONS=\"1\"\n\
         #EXTINF:6.0,\nseg1.ts\n#EXT-X-ENDLIST\n",
    )
}

async fn drm_wv_playlist() -> Response {
    playlist_response(
        "#EXTM3U\n\
         #EXT-X-KEY:METHOD=AES-128,URI=\"data://key\",KEYFORMAT=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"\n\
         #EXTINF:6.0,\nseg1.ts\n#EXT-X-ENDLIST\n",
    )
}

async fn clean_mpd() -> Response {
    let mut resp = (
        StatusCode::OK,
        r#"<?xml version="1.0"?><MPD xmlns="urn:mpeg:dash:schema:mpd:2011"><Period><AdaptationSet><Representation id="1"/></AdaptationSet></Period></MPD>"#,
    )
        .into_response();
    resp.headers_mut().insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static("application/dash+xml"),
    );
    resp
}

async fn drm_fps_page(State(state): State<Arc<UpstreamState>>) -> Response {
    Html::from(format!(
        "<html><body><script>var src=\"{}/drm/fps.m3u8\";</script></body></html>",
        state.base
    ))
}

async fn drm_wv_page(State(state): State<Arc<UpstreamState>>) -> Response {
    Html::from(format!(
        "<html><body><script>var src=\"{}/drm/wv.m3u8\";</script></body></html>",
        state.base
    ))
}

async fn clean_dash_page(State(state): State<Arc<UpstreamState>>) -> Response {
    Html::from(format!(
        "<html><body><video src=\"{}/drm/clean.mpd\"></video></body></html>",
        state.base
    ))
}

// --- D5：流失效夹具（主列表 200、变体 404；央视 4K 专区老片实测场景） ---

async fn dead_master_playlist(State(state): State<Arc<UpstreamState>>) -> Response {
    playlist_response_owned(format!(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=2048000,RESOLUTION=1280x720\n{}/dead/variant.m3u8\n",
        state.base
    ))
}

async fn dead_variant_404() -> Response {
    (StatusCode::NOT_FOUND, "NoSuchKey").into_response()
}

async fn dead_hls_page(State(state): State<Arc<UpstreamState>>) -> Response {
    Html::from(format!(
        "<html><body><script>var src=\"{}/dead/master.m3u8\";</script></body></html>",
        state.base
    ))
}

/// E8 夹具：模拟 vdn.apps.cntv.cn/api/getHttpVideoInfo.do 的 JSON 响应。
async fn cntv_video_info(State(state): State<Arc<UpstreamState>>) -> Response {
    axum::Json(serde_json::json!({
        "ack": "yes",
        "title": "夹具纪录片",
        "hls_url": format!("{}/m3u8/plain.m3u8", state.base),
        "is_protected": "0",
        "is_invalid_copyright": "0",
        "video": { "validChapterNum": 4, "chapters": [{"duration": "300.00", "url": ""}] }
    }))
    .into_response()
}

/// E9 夹具：模拟 api.bilibili.com/pgc/player/web/playurl。
/// fnval=1 → 整段 mp4（durl）；fnval=16 → DASH 音画分轨。
fn bili_dash_json(base: &str) -> serde_json::Value {
    serde_json::json!({"code":0,"result":{"is_preview":0,"is_drm":false,"quality":32,
    "durl": [],
    "dash": {
        "video": [{"id":32,"baseUrl": format!("{base}/v_da2-1-30032.m4s?upsig=x"),"height":480}],
        "audio": [{"id":30216,"baseUrl": format!("{base}/a_da2-1-30216.m4s?upsig=y")}]
    }}})
}

async fn bili_playurl(
    Query(q): Query<HashMap<String, String>>,
    State(state): State<Arc<UpstreamState>>,
) -> Response {
    let fnval = q.get("fnval").map(String::as_str).unwrap_or("");
    let body = if fnval == "1" {
        serde_json::json!({"code":0,"result":{"is_preview":0,"is_drm":false,"quality":16,
            "durl":[{"url": format!("{}/video.mp4?upsig=z", state.base)}]}})
    } else {
        bili_dash_json(&state.base)
    };
    axum::Json(body).into_response()
}

/// E9 夹具（DASH 兜底分支）：fnval=1 时 durl 为空，迫使解析器走 fnval=16。
async fn bili_playurl_dash_only(
    Query(q): Query<HashMap<String, String>>,
    State(state): State<Arc<UpstreamState>>,
) -> Response {
    let fnval = q.get("fnval").map(String::as_str).unwrap_or("");
    let body = if fnval == "1" {
        serde_json::json!({"code":0,"result":{"is_preview":0,"is_drm":false,"quality":16,"durl":[]}})
    } else {
        bili_dash_json(&state.base)
    };
    axum::Json(body).into_response()
}

/// E9c 夹具：B 站 x/web-interface/view（两分 P 稿件）。
async fn bili_view() -> Response {
    axum::Json(serde_json::json!({"code":0,"data":{
        "title": "夹具视频",
        "pages": [
            {"cid": 111, "page": 1, "part": "上"},
            {"cid": 222, "page": 2, "part": "下"}
        ]
    }}))
    .into_response()
}

/// E9c 夹具：B 站 x/player/playurl——cid 回显进 durl URL，供断言选集正确。
async fn bili_ugc_playurl(
    Query(q): Query<HashMap<String, String>>,
    State(state): State<Arc<UpstreamState>>,
) -> Response {
    let cid = q.get("cid").cloned().unwrap_or_default();
    axum::Json(
        serde_json::json!({"code":0,"data":{"is_preview":0,"is_drm":false,"quality":64,
        "durl":[{"url": format!("{}/ugc_{cid}.mp4?upsig=z", state.base)}]}}),
    )
    .into_response()
}

struct Html;
impl Html {
    fn from(s: String) -> Response {
        let mut resp = (StatusCode::OK, s).into_response();
        resp.headers_mut().insert(
            header::CONTENT_TYPE,
            HeaderValue::from_static("text/html; charset=utf-8"),
        );
        resp
    }
}

fn playlist_response(text: &str) -> Response {
    playlist_response_owned(text.to_string())
}

fn playlist_response_owned(text: String) -> Response {
    let mut resp = (StatusCode::OK, text).into_response();
    resp.headers_mut().insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static("application/vnd.apple.mpegurl"),
    );
    resp
}

async fn extract_format_for(page: &str) -> Format {
    let upstream = spawn_upstream().await;
    let extractor = Extractor::new("http://127.0.0.1:8321", RulePack::empty());
    let info = extractor
        .extract(&format!("{upstream}/page/{page}"))
        .await
        .unwrap();
    assert_eq!(info.formats.len(), 1, "{page} 应提取出 1 个 format");
    info.formats.into_iter().next().unwrap()
}

fn bili_endpoints(
    upstream: &str,
    pgc_path: &str,
) -> crayon_browser_core::extract::sites::BiliEndpoints {
    crayon_browser_core::extract::sites::BiliEndpoints {
        pgc: format!("{upstream}{pgc_path}"),
        ugc: format!("{upstream}/bili/x/player/playurl"),
        view: format!("{upstream}/bili/x/web-interface/view"),
    }
}

#[path = "fixtures/dash.rs"]
mod dash;
#[path = "fixtures/hls.rs"]
mod hls;
#[path = "fixtures/mp4.rs"]
mod mp4;
#[path = "fixtures/security.rs"]
mod security;
#[path = "fixtures/sites.rs"]
mod sites;
#[path = "fixtures/static_parse.rs"]
mod static_parse;
