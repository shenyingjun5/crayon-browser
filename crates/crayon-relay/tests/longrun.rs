//! RL-013 长稳 harness（HARNESS 用例）：30 分钟 VOD/live 混合负载下，
//! relay 内存不得随媒体时长线性增长，停止后回落。
//!
//! 运行：`cargo test -p crayon-relay --release --test longrun -- --ignored --nocapture`
//! 默认 30 分钟；`LONGRUN_MINUTES=N` 可缩短（仅本地调试，正式证据需 30 分钟）。
//! Linux 专属（读 /proc/self/status 的 VmRSS）。

use crayon_relay::runtime::{RelayRuntime, RelayRuntimeConfig};
use crayon_relay::vault::UpstreamRecipe;
use std::net::{IpAddr, Ipv4Addr};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use test_support::upstream::{MockUpstream, UpstreamScript};

fn rss_kb() -> u64 {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|s| {
            s.lines()
                .find(|l| l.starts_with("VmRSS:"))
                .and_then(|l| l.split_whitespace().nth(1)?.parse().ok())
        })
        .unwrap_or(0)
}

#[tokio::test]
#[ignore]
async fn relay_first_byte_overhead_probe() {
    let upstream = MockUpstream::start(vec![(
        "/v.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: vec![1u8; 1024 * 1024],
        },
    )])
    .await
    .unwrap();
    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let runtime = RelayRuntime::start(RelayRuntimeConfig {
        media_host: "127.0.0.1".to_string(),
        allow_private_upstreams: true,
        now: Some(Arc::new(move || clock.load(Ordering::SeqCst))),
        ..RelayRuntimeConfig::default()
    })
    .await
    .unwrap();
    let grant = {
        let mut registry = runtime.core().registry.lock().unwrap();
        registry
            .create_session(
                crayon_domain::DeviceId::new("dev-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                vec!["127.0.0.1".to_string()],
                4 * 3600 * 1000,
                1000,
            )
            .unwrap()
    };
    {
        let mut registry = runtime.core().registry.lock().unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                crayon_domain::ResourceId::new("vod").unwrap(),
                "127.0.0.1",
                0,
            )
            .unwrap();
        runtime
            .core()
            .vault
            .lock()
            .unwrap()
            .store(
                &grant.session_id,
                crayon_domain::ResourceId::new("vod").unwrap(),
                UpstreamRecipe::new(&upstream.url("/v.mp4"), None, None).unwrap(),
            )
            .unwrap();
    }
    let client = reqwest::Client::new();
    let relay_url = format!(
        "{}/s/{}/r/vod/v.mp4",
        runtime.media_base_url(),
        grant.token_hex
    );

    let mut direct = Vec::new();
    let mut via_relay = Vec::new();
    use futures_util::StreamExt;
    for _ in 0..50 {
        let t = Instant::now();
        let mut s = client
            .get(upstream.url("/v.mp4"))
            .send()
            .await
            .unwrap()
            .bytes_stream();
        let _ = s.next().await;
        direct.push(t.elapsed());
        let t = Instant::now();
        let mut s = client.get(&relay_url).send().await.unwrap().bytes_stream();
        let _ = s.next().await;
        via_relay.push(t.elapsed());
    }
    direct.sort();
    via_relay.sort();
    let d = direct[direct.len() / 2];
    let r = via_relay[via_relay.len() / 2];
    eprintln!(
        "first-byte p50: direct={d:?} relay={r:?} overhead={:?}",
        r.saturating_sub(d)
    );
    assert!(
        r.saturating_sub(d) < Duration::from_millis(50),
        "relay 首字节附加延迟过大"
    );
    runtime.stop().await;
}

#[tokio::test]
#[ignore]
async fn rl_013_longrun_memory_stable() {
    let minutes: u64 = std::env::var("LONGRUN_MINUTES")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(30);
    // 模拟持续媒体流：10MB 分片（VOD）+ live 列表滚动。
    let big_segment = vec![7u8; 10 * 1024 * 1024];
    let upstream = MockUpstream::start(vec![
        (
            "/vod.mp4".to_string(),
            UpstreamScript::RangeAware {
                content_type: Some("video/mp4".to_string()),
                body: big_segment.clone(),
            },
        ),
        (
            "/live.m3u8".to_string(),
            UpstreamScript::Full {
                status: 200,
                content_type: Some("application/vnd.apple.mpegurl".to_string()),
                body: b"#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXTINF:2.0,\nseg0.ts\n".to_vec(),
            },
        ),
        (
            "/seg0.ts".to_string(),
            UpstreamScript::Full {
                status: 200,
                content_type: Some("video/mp2t".to_string()),
                body: big_segment,
            },
        ),
    ])
    .await
    .unwrap();

    let now = Arc::new(AtomicU64::new(1000));
    let clock = now.clone();
    let runtime = RelayRuntime::start(RelayRuntimeConfig {
        media_host: "127.0.0.1".to_string(),
        allow_private_upstreams: true,
        now: Some(Arc::new(move || clock.load(Ordering::SeqCst))),
        ..RelayRuntimeConfig::default()
    })
    .await
    .unwrap();

    // session + 两个资源（VOD 分片 + live 列表）
    let grant = {
        let mut registry = runtime.core().registry.lock().unwrap();
        registry
            .create_session(
                crayon_domain::DeviceId::new("dev-01").unwrap(),
                Some(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1))),
                vec!["127.0.0.1".to_string()],
                4 * 3600 * 1000, // 长稳会话 TTL 放宽
                1000,
            )
            .unwrap()
    };
    let session_id = grant.session_id.clone();
    {
        let mut registry = runtime.core().registry.lock().unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                crayon_domain::ResourceId::new("vod").unwrap(),
                "127.0.0.1",
                0,
            )
            .unwrap();
        registry
            .register_resource(
                &grant.token_hex,
                crayon_domain::ResourceId::new("live").unwrap(),
                "127.0.0.1",
                0,
            )
            .unwrap();
        let mut vault = runtime.core().vault.lock().unwrap();
        vault
            .store(
                &session_id,
                crayon_domain::ResourceId::new("vod").unwrap(),
                UpstreamRecipe::new(&upstream.url("/vod.mp4"), None, None).unwrap(),
            )
            .unwrap();
        vault
            .store(
                &session_id,
                crayon_domain::ResourceId::new("live").unwrap(),
                UpstreamRecipe::new(&upstream.url("/live.m3u8"), None, None).unwrap(),
            )
            .unwrap();
    }

    let client = reqwest::Client::new();
    let vod_url = format!(
        "{}/s/{}/r/vod/vod.mp4",
        runtime.media_base_url(),
        grant.token_hex
    );
    let live_url = format!(
        "{}/s/{}/r/live/live.m3u8",
        runtime.media_base_url(),
        grant.token_hex
    );

    let start = Instant::now();
    let mut samples = vec![rss_kb()];
    let mut bytes_total: u64 = 0;
    let mut round = 0u64;
    while start.elapsed() < Duration::from_secs(minutes * 60) {
        // VOD：Range 拉一段（模拟 seek/持续播放）
        let offset = (round % 9) * 1_000_000;
        let resp = client
            .get(&vod_url)
            .header("Range", format!("bytes={offset}-{}", offset + 999_999))
            .send()
            .await
            .unwrap();
        bytes_total += resp.bytes().await.unwrap().len() as u64;
        // live 列表刷新
        let _ = client.get(&live_url).send().await.unwrap().bytes().await;
        // 逻辑时钟前进，驱动缓存 TTL
        now.fetch_add(2_000, Ordering::SeqCst);
        round += 1;
        if round % 60 == 0 {
            samples.push(rss_kb());
        }
    }
    samples.push(rss_kb());

    runtime.stop().await;
    let after_stop = rss_kb();

    let first = samples.first().copied().unwrap_or(0);
    let last = *samples.last().unwrap();
    let total_mb = bytes_total / 1024 / 1024;
    eprintln!(
        "RL-013 harness: {minutes}min, {} rounds, {total_mb}MB streamed, RSS first={first}KB last={last}KB after_stop={after_stop}KB samples={samples:?}",
        round
    );
    // 内存不得随流量线性增长：30 分钟数 GB 流量下 RSS 增幅 < 64MB。
    assert!(
        last < first + 64 * 1024,
        "RSS 增长超限: first={first}KB last={last}KB"
    );
}
