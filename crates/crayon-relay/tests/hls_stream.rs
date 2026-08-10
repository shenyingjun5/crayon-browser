//! HLS serving contract (MED-15): RL-010 改写链路端到端、RL-011 二进制
//! 字节一致、304/ETag 缓存续约、live TTL 更新。端到端：media_router +
//! HlsFetcher + NetworkGuard + MockUpstream，全部 loopback。

use crayon_domain::{DeviceId, ResourceId};
use crayon_relay::hls::stream::HlsFetcher;
use crayon_relay::network_guard::{NetworkGuard, NetworkGuardConfig};
use crayon_relay::router::{media_router, RelayCore};
use crayon_relay::session::DEFAULT_SESSION_TTL_MS;
use crayon_relay::vault::UpstreamRecipe;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use test_support::upstream::{MockUpstream, UpstreamScript};

struct Harness {
    base: String,
    token: String,
    upstream: MockUpstream,
    now: Arc<AtomicU64>,
}

fn m3u8(body: &str) -> UpstreamScript {
    UpstreamScript::Full {
        status: 200,
        content_type: Some("application/vnd.apple.mpegurl".to_string()),
        body: body.as_bytes().to_vec(),
    }
}

const MASTER: &str = "#EXTM3U\n\
#EXT-X-STREAM-INF:BANDWIDTH=800000\n\
low/index.m3u8\n";

const MEDIA_LIVE: &str = "#EXTM3U\n\
#EXT-X-TARGETDURATION:2\n\
#EXT-X-MEDIA-SEQUENCE:100\n\
#EXTINF:2.0,\n\
seg100.ts\n";

async fn harness(extra_routes: Vec<(String, UpstreamScript)>) -> Harness {
    let mut routes = vec![
        ("/master.m3u8".to_string(), m3u8(MASTER)),
        ("/low/index.m3u8".to_string(), m3u8(MEDIA_LIVE)),
        (
            "/low/seg100.ts".to_string(),
            UpstreamScript::Custom {
                status: 200,
                headers: vec![("Content-Type".to_string(), "video/mp2t".to_string())],
                body: b"ts-segment-bytes".to_vec(),
            },
        ),
    ];
    routes.extend(extra_routes);
    let upstream = MockUpstream::start(routes).await.unwrap();

    let guard = NetworkGuard::new(NetworkGuardConfig {
        allow_private_addresses: true, // 测试钩子：指向本机 mock
        ..NetworkGuardConfig::default()
    });
    let fetcher = Arc::new(HlsFetcher::new(guard));
    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let core = Arc::new(
        RelayCore::new(Arc::new(move || clock.load(Ordering::SeqCst)))
            .with_fetcher(fetcher.clone()),
    );
    fetcher.bind(&core);

    let token = {
        let mut registry = core.registry.lock().unwrap();
        let grant = registry
            .create_session(
                DeviceId::new("dev-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                vec!["127.0.0.1".to_string()],
                DEFAULT_SESSION_TTL_MS,
                1000,
            )
            .unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                ResourceId::new("master").unwrap(),
                "127.0.0.1",
                0,
            )
            .unwrap();
        grant.token_hex
    };
    let session_id = {
        let registry = core.registry.lock().unwrap();
        registry
            .authorize(
                &token,
                &ResourceId::new("master").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                1000,
            )
            .unwrap()
            .session_id
    };
    core.vault
        .lock()
        .unwrap()
        .store(
            &session_id,
            ResourceId::new("master").unwrap(),
            UpstreamRecipe::new(
                &upstream.url("/master.m3u8"),
                Some("https://example.com".to_string()),
                None,
            )
            .unwrap(),
        )
        .unwrap();

    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let addr: SocketAddr = listener.local_addr().unwrap();
    let router = media_router(core);
    tokio::spawn(async move {
        axum::serve(
            listener,
            router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
        .unwrap();
    });
    Harness {
        base: format!("http://{addr}"),
        token,
        upstream,
        now,
    }
}

fn client() -> reqwest::Client {
    reqwest::Client::builder().build().unwrap()
}

#[tokio::test]
async fn rl_010_master_to_segment_opaque_chain() {
    let h = harness(vec![]).await;
    // 1. master → 改写为 opaque variant 地址
    let resp = client()
        .get(format!("{}/s/{}/master.m3u8", h.base, h.token))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    assert_eq!(
        resp.headers().get("content-type").unwrap(),
        "application/vnd.apple.mpegurl"
    );
    assert_eq!(resp.headers().get("cache-control").unwrap(), "no-cache");
    let master = resp.text().await.unwrap();
    assert!(master.contains("#EXT-X-STREAM-INF"));
    let variant_path = master
        .lines()
        .find(|l| !l.starts_with('#'))
        .unwrap()
        .to_string();
    assert!(
        variant_path.starts_with(&format!("/s/{}/r/", h.token)),
        "{variant_path}"
    );
    assert!(
        variant_path.ends_with("/index.m3u8"),
        "装饰名保留: {variant_path}"
    );

    // 2. variant（media 列表）→ 分片改写为 opaque 地址
    let resp = client()
        .get(format!("{}{}", h.base, variant_path))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    let media = resp.text().await.unwrap();
    assert!(media.contains("#EXT-X-MEDIA-SEQUENCE:100"));
    let seg_path = media
        .lines()
        .find(|l| !l.starts_with('#'))
        .unwrap()
        .to_string();
    assert!(seg_path.starts_with(&format!("/s/{}/r/", h.token)));
    assert!(seg_path.ends_with("/seg100.ts"));

    // 3. 分片经 opaque 地址拉取
    let resp = client()
        .get(format!("{}{}", h.base, seg_path))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"ts-segment-bytes");
    // 上游录制：分片请求带了父 recipe 的 Referer
    let all_requests = h.upstream.requests();
    let seg_reqs: Vec<_> = all_requests
        .iter()
        .filter(|r| r.path == "/low/seg100.ts")
        .collect();
    assert_eq!(seg_reqs.len(), 1);
    assert_eq!(seg_reqs[0].header("referer"), Some("https://example.com"));
}

#[tokio::test]
async fn rl_011_binary_segment_is_byte_exact() {
    let h = harness(vec![]).await;
    let master = client()
        .get(format!("{}/s/{}/master.m3u8", h.base, h.token))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    let variant_path = master
        .lines()
        .find(|l| !l.starts_with('#'))
        .unwrap()
        .to_string();
    let media = client()
        .get(format!("{}{}", h.base, variant_path))
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    let seg_path = media
        .lines()
        .find(|l| !l.starts_with('#'))
        .unwrap()
        .to_string();
    let body = client()
        .get(format!("{}{}", h.base, seg_path))
        .send()
        .await
        .unwrap()
        .bytes()
        .await
        .unwrap();
    // 与 upstream 字节一致（不经文本转换）
    assert_eq!(body.as_ref(), b"ts-segment-bytes");
}

#[tokio::test]
async fn playlist_cache_304_and_live_refresh() {
    let etag_routes = vec![(
        "/master.m3u8".to_string(),
        UpstreamScript::Custom {
            status: 200,
            headers: vec![
                (
                    "Content-Type".to_string(),
                    "application/vnd.apple.mpegurl".to_string(),
                ),
                ("ETag".to_string(), "\"v1\"".to_string()),
            ],
            body: MASTER.as_bytes().to_vec(),
        },
    )];
    let h = harness(etag_routes).await;
    let url = format!("{}/s/{}/master.m3u8", h.base, h.token);

    let first = client().get(&url).send().await.unwrap();
    assert_eq!(first.status(), 200);
    assert_eq!(first.headers().get("etag").unwrap(), "\"v1\"");
    let hits_after_first = h.upstream.hit_count("/master.m3u8");

    // TTL 内 + If-None-Match 匹配 → 304，不再访问上游
    let resp = client()
        .get(&url)
        .header("If-None-Match", "\"v1\"")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 304);
    assert_eq!(h.upstream.hit_count("/master.m3u8"), hits_after_first);

    // TTL 过期（live 列表 TARGETDURATION=2s → clamp 下限 1s... master 无 ENDLIST 且无
    // TARGETDURATION → 静态 60s；推进逻辑时钟越过 60s）→ 重新拉取并携带 If-None-Match
    h.now.fetch_add(61_000, Ordering::SeqCst);
    h.upstream.set_route(
        "/master.m3u8",
        UpstreamScript::Custom {
            status: 200,
            headers: vec![
                (
                    "Content-Type".to_string(),
                    "application/vnd.apple.mpegurl".to_string(),
                ),
                ("ETag".to_string(), "\"v1\"".to_string()),
            ],
            body: MASTER.as_bytes().to_vec(),
        },
    );
    let resp = client().get(&url).send().await.unwrap();
    assert_eq!(resp.status(), 200);
    let all_requests = h.upstream.requests();
    let master_reqs: Vec<_> = all_requests
        .iter()
        .filter(|r| r.path == "/master.m3u8")
        .collect();
    assert_eq!(
        master_reqs.last().unwrap().header("if-none-match"),
        Some("\"v1\""),
        "过期后回源携带条件头"
    );
}

#[tokio::test]
async fn live_playlist_updates_propagate_after_ttl() {
    let h = harness(vec![]).await;
    let url = format!("{}/s/{}/master.m3u8", h.base, h.token);
    let first = client()
        .get(&url)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(first.contains("#EXT-X-STREAM-INF"));

    // 上游更新内容（模拟 live 列表滚动）
    h.upstream.set_route(
        "/master.m3u8",
        m3u8("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900000\nmid/index.m3u8\n"),
    );
    h.upstream.set_route("/mid/index.m3u8", m3u8(MEDIA_LIVE));

    // TTL 内仍服务缓存
    let cached = client()
        .get(&url)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(cached.contains("BANDWIDTH=800000"), "TTL 内服务缓存");

    // TTL 过后取到新列表
    h.now.fetch_add(61_000, Ordering::SeqCst);
    let fresh = client()
        .get(&url)
        .send()
        .await
        .unwrap()
        .text()
        .await
        .unwrap();
    assert!(fresh.contains("BANDWIDTH=900000"), "TTL 过后 live 更新生效");
}
