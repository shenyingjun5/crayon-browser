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
use futures_util::StreamExt;
use get_video::extract::{parse_html, Extractor, Format, Protocol, RulePack};
use get_video::relay::{self, RelayConfig};
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
        get_video::encode_url_component(target)
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

// ---------------------------------------------------------------------------
// E2-E7：L1 静态解析夹具
// ---------------------------------------------------------------------------

#[test]
fn e2_escaped_m3u8_link() {
    let html = r#"<html><body><script>var url = "https:\/\/cdn.example.com\/a\/b.m3u8";</script></body></html>"#;
    let r = parse_html(html, "https://site.example/watch");
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/a/b.m3u8" && c.protocol == Protocol::Hls),
        "转义还原失败: {:?}",
        r.candidates
    );
}

#[test]
fn e3_percent_encoded_m3u8_link() {
    let html = r#"<html><body><script>var cfg="https%3A%2F%2Fcdn.example.com%2Fv%2Findex.m3u8%3Ftoken%3Dabc";</script></body></html>"#;
    let r = parse_html(html, "https://site.example/watch");
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/v/index.m3u8?token=abc"),
        "percent 解码失败: {:?}",
        r.candidates
    );
}

#[test]
fn e4_jsonld_video_object() {
    let html = r##"<html><head>
<script type="application/ld+json">{"@context":"https://schema.org","@type":"VideoObject","name":"测试视频标题","contentUrl":"https://cdn.example.com/media/movie.mp4"}</script>
</head><body></body></html>"##;
    let r = parse_html(html, "https://site.example/watch");
    assert_eq!(r.title.as_deref(), Some("测试视频标题"));
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/media/movie.mp4"
                && c.protocol == Protocol::Mp4)
    );
}

#[test]
fn e5_maccms_player_config() {
    let html = r#"<html><body><script>
var player_aaaa={"flag":"play","encrypt":0,"trysee":0,"points":0,"link":"/vod/1.html","link_next":"","link_pre":"","url":"https:\/\/cdn.example.com\/2026\/07\/index.m3u8","url_next":"","from":"m3u8","server":"no","note":"","id":"1"}
</script></body></html>"#;
    let r = parse_html(html, "https://maccms.example/vod/1.html");
    assert!(
        r.candidates
            .iter()
            .any(|c| c.url == "https://cdn.example.com/2026/07/index.m3u8"),
        "maccms 提取失败: {:?}",
        r.candidates
    );
}

#[test]
fn e5_maccms_encrypt_urlencoded() {
    let html = r#"<html><body><script>
var player_aaaa={"flag":"play","encrypt":1,"url":"https%3A%2F%2Fcdn.example.com%2Fenc%2Findex.m3u8","from":"m3u8"}
</script></body></html>"#;
    let r = parse_html(html, "https://maccms.example/vod/2.html");
    assert!(r
        .candidates
        .iter()
        .any(|c| c.url == "https://cdn.example.com/enc/index.m3u8"));
}

#[test]
fn e6_no_video_page() {
    let html = "<html><head><title>纯文本页</title></head><body><p>没有任何视频</p></body></html>";
    let r = parse_html(html, "https://site.example/text");
    assert!(r.candidates.is_empty());
}

#[test]
fn e7_dedup_and_quality_sort() {
    let html = r#"<html><body>
<p>1080p: https://cdn.example.com/v/master.m3u8</p>
<p>重复1: https://cdn.example.com/v/master.m3u8</p>
<p>重复2: "https:\/\/cdn.example.com\/v\/master.m3u8"</p>
<p>720p: https://cdn.example.com/v/720p/index.m3u8</p>
<p>备份: https://cdn.example.com/v/movie.mp4</p>
</body></html>"#;
    let r = parse_html(html, "https://site.example/watch");
    let urls: Vec<&str> = r.candidates.iter().map(|c| c.url.as_str()).collect();
    // 去重：master.m3u8 只出现一次
    assert_eq!(
        urls.iter()
            .filter(|u| **u == "https://cdn.example.com/v/master.m3u8")
            .count(),
        1,
        "去重失败: {urls:?}"
    );
    assert_eq!(urls.len(), 3);
    // 清晰度降序：1080p（带 1080p 上下文）在最前
    assert_eq!(
        urls[0], "https://cdn.example.com/v/master.m3u8",
        "清晰度排序失败: {urls:?}"
    );
    assert_eq!(r.candidates[0].quality, Some(1080));
    assert_eq!(r.candidates[1].quality, Some(720));
}

// ---------------------------------------------------------------------------
// R5-R8：m3u8 重写夹具
// ---------------------------------------------------------------------------

#[tokio::test]
async fn r5_ext_x_map_rewrite() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/m3u8/map.m3u8"), "");
    let body = client.get(&url).send().await.unwrap().text().await.unwrap();
    assert!(
        body.contains(&format!(
            "#EXT-X-MAP:URI=\"{}/proxy/{}/init.mp4\"",
            relay.base_url(),
            get_video::encode_url_component(&format!("{upstream}/m3u8/init.mp4"))
        )),
        "MAP URI 未改写: {body}"
    );
    assert!(body.contains("/proxy/"), "分片行未改写: {body}");
    relay.shutdown().await;
}

#[tokio::test]
async fn r6_recursive_master_depth_limit() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    // chain0 → chain1 → ... → chain5 正常（depth 0..=5），chain6 请求 depth=6 → 400
    let mut current = format!("{upstream}/m3u8/chain0.m3u8");
    let mut ok_count = 0;
    loop {
        let url = if ok_count == 0 {
            proxy_url(&relay.base_url(), &current, "")
        } else {
            // current 已经是 relay 重写后的地址
            current.clone()
        };
        let resp = client.get(&url).send().await.unwrap();
        if resp.status() == StatusCode::BAD_REQUEST {
            break;
        }
        assert_eq!(resp.status(), StatusCode::OK, "中间层失败: {url}");
        let body = resp.text().await.unwrap();
        ok_count += 1;
        // 找下一层地址
        let next = body
            .lines()
            .map(|l| l.trim())
            .find(|l| !l.is_empty() && !l.starts_with('#'))
            .expect("chain 里没有下一层");
        current = next.to_string();
    }
    assert_eq!(ok_count, 6, "应允许 depth 0..=5 共 6 层，实际 {ok_count}");
    relay.shutdown().await;
}

#[tokio::test]
async fn r7_content_type_plain_but_m3u8_body() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(
        &relay.base_url(),
        &format!("{upstream}/m3u8/plain.m3u8"),
        "",
    );
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get(header::CONTENT_TYPE)
        .unwrap()
        .to_str()
        .unwrap()
        .to_string();
    let body = resp.text().await.unwrap();
    assert!(
        ct.contains("mpegurl"),
        "按内容判定后应输出 m3u8 Content-Type，实际 {ct}"
    );
    assert!(body.contains("/proxy/"), "按内容判定应走重写: {body}");
    relay.shutdown().await;
}

#[tokio::test]
async fn r8_segment_with_query() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(
        &relay.base_url(),
        &format!("{upstream}/m3u8/query.m3u8"),
        "",
    );
    let body = client.get(&url).send().await.unwrap().text().await.unwrap();
    let target = first_proxied_target(&body).expect("未找到改写后的分片地址");
    assert_eq!(
        target,
        format!("{upstream}/seg.ts?token=abc&x=1"),
        "query 丢失或错乱: {target}"
    );
    // 顺着改写地址拉分片，mock 上游回显 query，字节级断言
    let seg_line = body
        .lines()
        .map(|l| l.trim())
        .find(|l| !l.is_empty() && !l.starts_with('#'))
        .unwrap()
        .to_string();
    let seg_body = client
        .get(&seg_line)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert_eq!(seg_body, "seg-bytes:token=abc&x=1");
    relay.shutdown().await;
}

// ---------------------------------------------------------------------------
// R10 / R12：Range 与传输夹具
// ---------------------------------------------------------------------------

#[tokio::test]
async fn r10_upstream_without_range_support() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/no-range"), "");
    let resp = client
        .get(&url)
        .header(header::RANGE, "bytes=0-3")
        .send()
        .await
        .unwrap();
    // relay 透传 200，不伪造 206
    assert_eq!(resp.status(), StatusCode::OK);
    assert!(resp.headers().get(header::CONTENT_RANGE).is_none());
    assert_eq!(resp.text().await.unwrap(), "0123456789abcdef");
    relay.shutdown().await;
}

#[tokio::test]
async fn r12_large_file_streaming() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/big"), "");
    let start = Instant::now();
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let mut stream = resp.bytes_stream();
    // 首字节延迟必须远小于全量耗时（上游每块 5ms，50MB 全量约 1s+）
    let first = stream.next().await.unwrap().unwrap();
    let ttfb = start.elapsed();
    assert!(!first.is_empty());
    assert!(
        ttfb < Duration::from_millis(800),
        "首字节延迟 {ttfb:?}，疑似非流式转发"
    );
    // 边下边读，收满 50MB
    let mut total = first.len();
    while let Some(chunk) = stream.next().await {
        total += chunk.unwrap().len();
    }
    assert_eq!(total, 50 * 1024 * 1024);
    relay.shutdown().await;
}

// ---------------------------------------------------------------------------
// R13-R16：防盗链与头部夹具
// ---------------------------------------------------------------------------

#[tokio::test]
async fn r13_referer_spoofing() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let target = format!("{upstream}/guard/referer");
    // 带 referer 参数 → 200
    let ok = client
        .get(proxy_url(
            &relay.base_url(),
            &target,
            "referer=http%3A%2F%2Fallowed.example%2F",
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(ok.status(), StatusCode::OK);
    assert_eq!(ok.text().await.unwrap(), "guard-pass");
    // 不带 referer → relay 默认用目标 origin，mock 源拒绝 → 403 透传
    let blocked = client
        .get(proxy_url(&relay.base_url(), &target, ""))
        .send()
        .await
        .unwrap();
    assert_eq!(blocked.status(), StatusCode::FORBIDDEN);
    relay.shutdown().await;
}

#[tokio::test]
async fn r14_user_agent() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let target = format!("{upstream}/guard/ua");
    // 默认桌面浏览器 UA → 200
    let ok = client
        .get(proxy_url(&relay.base_url(), &target, ""))
        .send()
        .await
        .unwrap();
    assert_eq!(ok.status(), StatusCode::OK);
    // 客户端自带 UA 不应透传给上游（客户端用 BadBot，relay 仍用默认浏览器 UA）
    let ok2 = client
        .get(proxy_url(&relay.base_url(), &target, ""))
        .header(header::USER_AGENT, "BadBot/1.0")
        .send()
        .await
        .unwrap();
    assert_eq!(ok2.status(), StatusCode::OK);
    // 显式 ua 参数覆盖
    let blocked = client
        .get(proxy_url(&relay.base_url(), &target, "ua=BadBot%2F1.0"))
        .send()
        .await
        .unwrap();
    assert_eq!(blocked.status(), StatusCode::FORBIDDEN);
    relay.shutdown().await;
}

#[tokio::test]
async fn r15_sensitive_headers_stripped() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/sensitive"), "");
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    assert!(
        resp.headers().get(header::SET_COOKIE).is_none(),
        "set-cookie 未净化"
    );
    assert!(resp.headers().get(header::X_FRAME_OPTIONS).is_none());
    assert!(resp.headers().get("content-security-policy").is_none());
    assert_eq!(resp.text().await.unwrap(), "sensitive-body");
    relay.shutdown().await;
}

#[tokio::test]
async fn r16_cors() {
    let upstream = spawn_upstream().await;
    let relay = spawn_relay(true).await;
    let client = reqwest::Client::new();
    let url = proxy_url(&relay.base_url(), &format!("{upstream}/sensitive"), "");
    // GET 带 ACAO:*
    let resp = client.get(&url).send().await.unwrap();
    assert_eq!(
        resp.headers()
            .get(header::ACCESS_CONTROL_ALLOW_ORIGIN)
            .unwrap(),
        "*"
    );
    // OPTIONS 预检 → 204
    let preflight = client
        .request(reqwest::Method::OPTIONS, &url)
        .header(header::ORIGIN, "http://player.example")
        .header(header::ACCESS_CONTROL_REQUEST_METHOD, "GET")
        .send()
        .await
        .unwrap();
    assert_eq!(preflight.status(), StatusCode::NO_CONTENT);
    assert_eq!(
        preflight
            .headers()
            .get(header::ACCESS_CONTROL_ALLOW_ORIGIN)
            .unwrap(),
        "*"
    );
    relay.shutdown().await;
}

// ---------------------------------------------------------------------------
// S1-S3：安全夹具（relay 不开 allow_private_hosts）
// ---------------------------------------------------------------------------

#[tokio::test]
async fn s1_ssrf_blocked() {
    let relay = spawn_relay(false).await;
    let client = reqwest::Client::new();
    for target in [
        "http://127.0.0.1:8321/x",
        "http://localhost/secret",
        "http://192.168.1.1/admin",
        "http://10.0.0.1/internal",
        "http://169.254.169.254/latest/meta-data",
        "http://172.16.0.1/x",
    ] {
        let url = proxy_url(&relay.base_url(), target, "");
        let resp = client.get(&url).send().await.unwrap();
        assert_eq!(
            resp.status(),
            StatusCode::BAD_REQUEST,
            "{target} 应被 400 拒绝"
        );
    }
    relay.shutdown().await;
}

#[tokio::test]
async fn s2_non_http_scheme() {
    let relay = spawn_relay(false).await;
    let client = reqwest::Client::new();
    for target in ["file:///etc/passwd", "ftp://example.com/x"] {
        let url = proxy_url(&relay.base_url(), target, "");
        let resp = client.get(&url).send().await.unwrap();
        assert_eq!(
            resp.status(),
            StatusCode::BAD_REQUEST,
            "{target} 应被 400 拒绝"
        );
    }
    relay.shutdown().await;
}

#[tokio::test]
async fn s3_malformed_encoding() {
    let relay = spawn_relay(false).await;
    let client = reqwest::Client::new();
    // 非法 percent 编码：直接拼原始请求路径（不经 Url 规范化）
    for path in ["/proxy/bad%zz", "/proxy/%2", "/proxy/100%"] {
        let url = format!("{}{}", relay.base_url(), path);
        let resp = client.get(&url).send().await.unwrap();
        assert_eq!(
            resp.status(),
            StatusCode::BAD_REQUEST,
            "{path} 应被 400 拒绝"
        );
    }
    relay.shutdown().await;
}

// ---------------------------------------------------------------------------
// D1 / D2 / D4-夹具：DRM 检测（extract 层，验证 drm 标记与不产出 relay 地址）
// ---------------------------------------------------------------------------

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

#[tokio::test]
async fn d1_fairplay_marked_drm() {
    let f = extract_format_for("drm_fps.html").await;
    assert!(f.drm, "FairPlay 应标记 drm:true");
    assert!(f.relay_url.is_none(), "DRM 内容不产出 relay 地址");
}

#[tokio::test]
async fn d2_widevine_keyformat_marked_drm() {
    let f = extract_format_for("drm_wv.html").await;
    assert!(f.drm, "Widevine KEYFORMAT 应标记 drm:true");
    assert!(f.relay_url.is_none());
}

#[tokio::test]
async fn d4_dash_vod_no_drm() {
    let f = extract_format_for("clean_dash.html").await;
    assert!(!f.drm, "无 ContentProtection 的 DASH 应为 drm:false");
    assert!(f.relay_url.is_some());
    assert_eq!(f.protocol, "dash");
}

/// D5：主列表 200 但变体 404 → 标受限「流地址已失效」，不产出 relay 地址。
/// （央视 4K 专区老片实测场景：CDN 清档只剩主列表）
#[tokio::test]
async fn d5_dead_variant_marked_restricted() {
    let f = extract_format_for("dead_hls.html").await;
    assert!(!f.drm, "失效不是 DRM");
    let reason = f.restriction.expect("变体 404 应标受限");
    assert!(reason.contains("HTTP 404"), "原因应含状态码: {reason}");
    assert!(f.relay_url.is_none(), "失效流不产出 relay 地址");
}

// ---------------------------------------------------------------------------
// E8：站点专用解析器（央视 cntv）夹具
// ---------------------------------------------------------------------------

/// E8：央视纪录片页面 HTML 只有 guid，播放地址经「guid → 站点 API → JSON」
/// 两步拿到——L1 正则扫不到，站点解析器补齐。
#[tokio::test]
async fn e8_cntv_site_extractor() {
    let upstream = spawn_upstream().await;
    let html = r#"<html><head><script>var guid = "4646c21e429d43a08eac19d18704c4e9";</script></head></html>"#;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_cntv(
        &client,
        html,
        &format!("{upstream}/cntv/getHttpVideoInfo.do"),
    )
    .await
    .expect("应命中央视站点解析器");
    assert_eq!(r.title.as_deref(), Some("夹具纪录片"));
    assert_eq!(r.candidates.len(), 1, "空 chapter 地址应被过滤");
    assert_eq!(r.candidates[0].url, format!("{upstream}/m3u8/plain.m3u8"));
    assert_eq!(r.candidates[0].protocol, Protocol::Hls);
    // guid 缺失时不命中
    assert!(get_video::extract::sites::extract_cntv(
        &client,
        "<html>no guid here</html>",
        &format!("{upstream}/cntv/getHttpVideoInfo.do"),
    )
    .await
    .is_none());
}

// ---------------------------------------------------------------------------
// E9：站点专用解析器（B 站：番剧 ep/ss + 普通 BV 页）夹具
// ---------------------------------------------------------------------------

fn bili_endpoints(upstream: &str, pgc_path: &str) -> get_video::extract::sites::BiliEndpoints {
    get_video::extract::sites::BiliEndpoints {
        pgc: format!("{upstream}{pgc_path}"),
        ugc: format!("{upstream}/bili/x/player/playurl"),
        view: format!("{upstream}/bili/x/web-interface/view"),
    }
}

/// E9a：番剧 ep 页——DASH 480P 高于整段 360P 时 dash 合成候选在前，整段保留为低清备选。
#[tokio::test]
async fn e9a_bilibili_durl_preferred() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ep733316?spm_id_from=333.337.0.0",
        "",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .expect("应命中 B 站解析器");
    assert_eq!(r.candidates.len(), 2, "dash 高清合成候选 + 整段低清备选");
    assert_eq!(r.candidates[0].protocol, Protocol::Dash);
    assert_eq!(r.candidates[0].quality, Some(480));
    assert_eq!(
        r.candidates[0].url,
        format!("{upstream}/v_da2-1-30032.m4s?upsig=x")
    );
    assert_eq!(
        r.candidates[0].audio_url.as_deref(),
        Some(format!("{upstream}/a_da2-1-30216.m4s?upsig=y").as_str())
    );
    assert_eq!(r.candidates[1].url, format!("{upstream}/video.mp4?upsig=z"));
    assert_eq!(r.candidates[1].protocol, Protocol::Mp4);
    assert_eq!(r.candidates[1].quality, Some(360));
    assert_eq!(r.referer.as_deref(), Some("https://www.bilibili.com"));
    assert!(r.note.is_none());
    // 既非番剧也非视频页的 URL 不命中
    assert!(get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/",
        "",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .is_none());
}

/// E9b：番剧 ep 页——durl 为空时输出 DASH 合成候选（视频轨+音频轨一体，经 relay 出 MPD）。
#[tokio::test]
async fn e9b_bilibili_dash_fallback() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ep733316",
        "",
        &bili_endpoints(&upstream, "/bili_dash/pgc/playurl"),
        None,
    )
    .await
    .expect("应命中 B 站解析器");
    assert_eq!(r.candidates.len(), 1, "无整段时仅 dash 合成候选");
    let c = &r.candidates[0];
    assert_eq!(c.protocol, Protocol::Dash);
    assert_eq!(c.quality, Some(480));
    assert!(c.audio_url.is_some(), "合成候选携带音频轨地址");
    assert!(r.note.is_none());
}

/// E9c：普通 BV 视频页——view 换 cid，?p=2 选第二集，ugc playurl 出整段。
#[tokio::test]
async fn e9c_bilibili_bv_multi_page() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/video/BV1xx411c7mD?p=2",
        "",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .expect("应命中 BV 页解析");
    assert_eq!(r.candidates.len(), 1);
    assert!(
        r.candidates[0].url.contains("ugc_222"),
        "?p=2 应选 cid=222 的分 P: {}",
        r.candidates[0].url
    );
    assert_eq!(r.candidates[0].quality, Some(720));
    assert_eq!(r.title.as_deref(), Some("夹具视频 P2 下"));
}

/// E9d：番剧 ss 季页——HTML 里的默认集 ep_id 转 pgc/playurl。
#[tokio::test]
async fn e9d_bilibili_ss_season_page() {
    let upstream = spawn_upstream().await;
    let client = reqwest::Client::new();
    let html = r#"<script>window.__INITIAL_STATE__={"epInfo":{"ep_id":733316}};</script>"#;
    let r = get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ss28747",
        html,
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .expect("ss 页应经默认集 ep_id 命中");
    assert_eq!(r.candidates.len(), 2, "dash 合成候选 + 整段备选");
    assert_eq!(r.candidates[1].url, format!("{upstream}/video.mp4?upsig=z"));
    // ss 页 HTML 无 ep_id 时不命中
    assert!(get_video::extract::sites::extract_bilibili(
        &client,
        "https://www.bilibili.com/bangumi/play/ss28747",
        "<html>nothing</html>",
        &bili_endpoints(&upstream, "/bili/pgc/playurl"),
        None,
    )
    .await
    .is_none());
}
