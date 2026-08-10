//! RelayRuntime contract (MED-16): 装配与绑定、RL-005 触发器收口（registry
//! + vault 同步撤销）、RL-012 并发上限、幂等优雅停止。
//!
//! 端到端走真实 composite fetcher + MockUpstream，全部 loopback。

use crayon_domain::{DeviceId, ResourceId};
use crayon_relay::runtime::{RelayRuntime, RelayRuntimeConfig};
use crayon_relay::session::RevokeReason;
use crayon_relay::vault::UpstreamRecipe;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use test_support::upstream::{drip, MockUpstream};

const SECRET: &str = "runtime-test-secret";

fn config(secret: &str, now: Arc<AtomicU64>, max_concurrent: usize) -> RelayRuntimeConfig {
    RelayRuntimeConfig {
        media_host: "127.0.0.1".to_string(),
        control_secret: secret.to_string(),
        max_concurrent_media: max_concurrent,
        allow_private_upstreams: true, // 测试钩子：本机 mock
        now: Some(Arc::new(move || now.load(Ordering::SeqCst))),
        ..RelayRuntimeConfig::default()
    }
}

async fn start_with_movie() -> (Arc<RelayRuntime>, MockUpstream, String, Arc<AtomicU64>) {
    let (script, control) = drip(200, Some("video/mp4".to_string()), vec![b"movie".to_vec()]);
    control.release(1);
    let upstream = MockUpstream::start(vec![("/movie.mp4".to_string(), script)])
        .await
        .unwrap();
    let now = Arc::new(AtomicU64::new(1000));
    let runtime = RelayRuntime::start(config(SECRET, now.clone(), 64))
        .await
        .unwrap();

    // 控制面创建 session
    let resp = reqwest::Client::new()
        .post(format!(
            "{}/internal/cast/session",
            runtime.control_base_url()
        ))
        .header("x-crayon-control-secret", SECRET)
        .json(&serde_json::json!({
            "receiver_id": "dev-01",
            "receiver_ip": "127.0.0.1",
            "upstream_allow_set": ["127.0.0.1"]
        }))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    let created: serde_json::Value = resp.json().await.unwrap();
    let token = created["token"].as_str().unwrap().to_string();

    // 编排层经 core 注册资源与 recipe（生产路径由 MED-17 delivery 调用）
    let session_id = created["session_id"].as_str().unwrap().to_string();
    let session_id = crayon_domain::SessionId::new(&session_id).unwrap();
    {
        let mut registry = runtime.core().registry.lock().unwrap();
        registry
            .register_resource(&token, ResourceId::new("res-01").unwrap(), "127.0.0.1", 0)
            .unwrap();
        runtime
            .core()
            .vault
            .lock()
            .unwrap()
            .store(
                &session_id,
                ResourceId::new("res-01").unwrap(),
                UpstreamRecipe::new(&upstream.url("/movie.mp4"), None, None).unwrap(),
            )
            .unwrap();
    }
    (runtime, upstream, token, now)
}

#[tokio::test]
async fn end_to_end_through_runtime() {
    let (runtime, _upstream, token, _now) = start_with_movie().await;
    let resp = reqwest::Client::new()
        .get(format!(
            "{}/s/{}/r/res-01/movie.mp4",
            runtime.media_base_url(),
            token
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 200);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"movie");
    runtime.stop().await;
}

#[tokio::test]
async fn rl_005_triggers_revoke_sessions_and_recipes() {
    let (runtime, _upstream, token, _now) = start_with_movie().await;
    assert_eq!(runtime.trigger(RevokeReason::Navigation, None), 1);
    let resp = reqwest::Client::new()
        .get(format!(
            "{}/s/{}/r/res-01/movie.mp4",
            runtime.media_base_url(),
            token
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 401, "导航后 session 立即失效");
    assert_eq!(
        runtime
            .core()
            .vault
            .lock()
            .unwrap()
            .session_len(&crayon_domain::SessionId::new("sess-0000000000000001").unwrap()),
        0,
        "recipe 同步清理"
    );
    runtime.stop().await;
}

#[tokio::test]
async fn route_lost_only_revokes_that_device() {
    let (runtime, _upstream, token, _now) = start_with_movie().await;
    // 第二设备 session
    let resp = reqwest::Client::new()
        .post(format!(
            "{}/internal/cast/session",
            runtime.control_base_url()
        ))
        .header("x-crayon-control-secret", SECRET)
        .json(&serde_json::json!({
            "receiver_id": "dev-02",
            "receiver_ip": "127.0.0.1",
            "upstream_allow_set": ["127.0.0.1"]
        }))
        .send()
        .await
        .unwrap();
    let token2: serde_json::Value = resp.json().await.unwrap();
    let token2 = token2["token"].as_str().unwrap().to_string();

    assert_eq!(
        runtime.trigger(
            RevokeReason::RouteLost,
            Some(&DeviceId::new("dev-01").unwrap())
        ),
        1
    );
    let client = reqwest::Client::new();
    let gone = client
        .get(format!(
            "{}/s/{}/r/res-01/movie.mp4",
            runtime.media_base_url(),
            token
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(gone.status(), 401);
    // dev-02 的 session 不受影响（其资源未注册，401/404 不算 401-unknown-session…
    // 只验证 session 仍存在：用 dev-02 token 访问得到 404 而非 401）
    let other = client
        .get(format!(
            "{}/s/{}/r/res-01/movie.mp4",
            runtime.media_base_url(),
            token2
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(other.status(), 404, "dev-02 session 存活（资源未注册）");
    runtime.stop().await;
}

#[tokio::test]
async fn rl_012_media_plane_is_bounded() {
    let (script, _control) = drip(200, Some("video/mp4".to_string()), vec![b"x".to_vec()]);
    let upstream = MockUpstream::start(vec![("/movie.mp4".to_string(), script)])
        .await
        .unwrap();
    let now = Arc::new(AtomicU64::new(1000));
    let runtime = RelayRuntime::start(config(SECRET, now, 1)).await.unwrap();
    let resp = reqwest::Client::new()
        .post(format!(
            "{}/internal/cast/session",
            runtime.control_base_url()
        ))
        .header("x-crayon-control-secret", SECRET)
        .json(&serde_json::json!({
            "receiver_id": "dev-01",
            "receiver_ip": "127.0.0.1",
            "upstream_allow_set": ["127.0.0.1"]
        }))
        .send()
        .await
        .unwrap();
    let created: serde_json::Value = resp.json().await.unwrap();
    let token = created["token"].as_str().unwrap().to_string();
    let session_id =
        crayon_domain::SessionId::new(created["session_id"].as_str().unwrap()).unwrap();
    {
        let mut registry = runtime.core().registry.lock().unwrap();
        registry
            .register_resource(&token, ResourceId::new("res-01").unwrap(), "127.0.0.1", 0)
            .unwrap();
        runtime
            .core()
            .vault
            .lock()
            .unwrap()
            .store(
                &session_id,
                ResourceId::new("res-01").unwrap(),
                UpstreamRecipe::new(&upstream.url("/movie.mp4"), None, None).unwrap(),
            )
            .unwrap();
    }

    // 占用唯一并发槽（drip 永不放行，body 读取悬挂）
    let first = tokio::spawn({
        let url = format!(
            "{}/s/{}/r/res-01/movie.mp4",
            runtime.media_base_url(),
            token
        );
        async move {
            let resp = reqwest::Client::new().get(&url).send().await.unwrap();
            let _ = resp.bytes().await; // 永不完成：持有并发槽
        }
    });
    for _ in 0..1000 {
        if upstream.hit_count("/movie.mp4") == 1 {
            break;
        }
        tokio::task::yield_now().await;
    }
    let second = reqwest::Client::new()
        .get(format!(
            "{}/s/{}/r/res-01/movie.mp4",
            runtime.media_base_url(),
            token
        ))
        .send()
        .await
        .unwrap();
    assert_eq!(second.status(), 503, "满载有界拒绝而非排队堆积");
    first.abort();
    runtime.stop().await;
}

#[tokio::test]
async fn stop_is_idempotent_and_releases_ports() {
    let (runtime, _upstream, _token, _now) = start_with_movie().await;
    let media = runtime.media_base_url();
    runtime.stop().await;
    runtime.stop().await; // 幂等
    let result = reqwest::Client::new()
        .get(format!("{media}/internal/health"))
        .send()
        .await;
    assert!(result.is_err(), "停止后端口已释放");
    assert_eq!(
        runtime.core().registry.lock().unwrap().len(),
        0,
        "停止时撤销全部 session"
    );
}
