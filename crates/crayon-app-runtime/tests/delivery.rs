//! Delivery orchestration contract (MED-17, Mirror semantics migrated by
//! MED-19): decision→plan 映射、PL-014 普通失败不提权、单次降级无循环、
//! 外部客户端交接不触碰会话后端（E2E-002/004 的 fake 变体，PL-015）。

use crayon_app_runtime::delivery::{
    downgrade_once, plan_delivery, CoreSessionBackend, DeliveryPlan, DeliveryRequest,
    SessionBackend, StartOutcome,
};
use crayon_cast_policy::HandoffAvailability;
use crayon_domain::{CoreError, DeviceId, ReceiverCapabilities, TabId};
use crayon_ipc_schema::{
    AdContinuity, CastPolicyInput, ExternalClientHandoff, HandoffConfirmation, HandoffReason,
    HeadersClass, MediaCandidate, PageContext, PlaybackState, ProtocolKind,
};
use crayon_media_observer::{
    ObservationOrigin, PlaybackObservation, PlaybackProgress, UserActivation,
};
use crayon_media_probe::Protection;
use crayon_relay::runtime::{RelayRuntime, RelayRuntimeConfig};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use test_support::upstream::{MockUpstream, UpstreamScript};

fn verified() -> PlaybackObservation {
    PlaybackObservation::new(
        ObservationOrigin::BrowserVerified,
        UserActivation::BrowserVerified,
        PlaybackProgress::Advanced,
    )
}

fn request(headers: HeadersClass, protocol: ProtocolKind) -> DeliveryRequest {
    DeliveryRequest {
        input: CastPolicyInput::new(
            PageContext::new(
                TabId::new("tab-01").unwrap(),
                "https://example.com/watch".to_string(),
            ),
            PlaybackState::new(120.0, Some(3600.0), false),
            MediaCandidate::new(
                "https://cdn.example.com/media/movie.mp4?sign=abc".to_string(),
                protocol,
                false,
                headers,
                None,
                None,
                AdContinuity::Preserved,
            ),
            ReceiverCapabilities::new(true, true, true, true, true, true, 2160),
        ),
        observation: verified(),
        protection: Protection::Clear,
        external_client_handoff: HandoffAvailability::Available,
        receiver: DeviceId::new("dev-01").unwrap(),
        receiver_ip: None,
    }
}

/// 记录型假后端。
struct FakeBackend {
    calls: Mutex<Vec<String>>,
    fail_with: Option<CoreError>,
}

impl FakeBackend {
    fn new() -> Self {
        Self {
            calls: Mutex::new(Vec::new()),
            fail_with: None,
        }
    }
}

impl SessionBackend for FakeBackend {
    fn open(
        &mut self,
        _receiver: &DeviceId,
        _receiver_ip: Option<std::net::IpAddr>,
        candidate_url: &str,
        _protocol: ProtocolKind,
        _headers_class: HeadersClass,
        _page_url: &str,
    ) -> Result<String, CoreError> {
        self.calls.lock().unwrap().push(candidate_url.to_string());
        match self.fail_with {
            Some(error) => Err(error),
            None => Ok("http://192.168.1.8:20001/s/fake-token/master.m3u8".to_string()),
        }
    }
}

#[test]
fn direct_plan_never_touches_backend() {
    let mut backend = FakeBackend::new();
    let plan = plan_delivery(
        &request(HeadersClass::None, ProtocolKind::Mp4),
        &mut backend,
    );
    assert_eq!(
        plan,
        DeliveryPlan::Direct {
            url: "https://cdn.example.com/media/movie.mp4?sign=abc".to_string()
        }
    );
    assert!(backend.calls.lock().unwrap().is_empty());
}

#[test]
fn relay_plan_opens_session_via_backend() {
    let mut backend = FakeBackend::new();
    let plan = plan_delivery(
        &request(HeadersClass::RefererOnly, ProtocolKind::Hls),
        &mut backend,
    );
    assert!(matches!(plan, DeliveryPlan::Relay { .. }));
    assert_eq!(backend.calls.lock().unwrap().len(), 1);
}

#[test]
fn pl_014_backend_failure_rejects_without_escalation() {
    let mut backend = FakeBackend::new();
    backend.fail_with = Some(CoreError::CapabilitiesUnavailable);
    let plan = plan_delivery(
        &request(HeadersClass::RefererOnly, ProtocolKind::Hls),
        &mut backend,
    );
    assert_eq!(
        plan,
        DeliveryPlan::Rejected(CoreError::CapabilitiesUnavailable),
        "规划期普通失败直接拒绝，不提权不降级"
    );
}

#[test]
fn drm_and_credential_never_reach_backend() {
    for (protection, headers) in [
        (Protection::DrmProtected, HeadersClass::None),
        (Protection::Clear, HeadersClass::CredentialBound),
    ] {
        let mut backend = FakeBackend::new();
        let mut req = request(headers, ProtocolKind::Hls);
        req.protection = protection;
        let plan = plan_delivery(&req, &mut backend);
        match plan {
            DeliveryPlan::Rejected(_) | DeliveryPlan::ExternalClientHandoff(_) => {}
            other => panic!("应为 Rejected/ExternalClientHandoff: {other:?}"),
        }
        assert!(backend.calls.lock().unwrap().is_empty());
    }
}

#[test]
fn dash_relay_structurally_degrades_to_handoff_or_stable_reject() {
    // 有交接能力：DASH relay 不在 v1，结构化降级为外部交接建议。
    let mut backend = FakeBackend::new();
    let plan = plan_delivery(
        &request(HeadersClass::RefererOnly, ProtocolKind::Dash),
        &mut backend,
    );
    assert_eq!(
        plan,
        DeliveryPlan::ExternalClientHandoff(ExternalClientHandoff::new(
            HandoffReason::DashRelayUnsupported
        )),
        "DASH relay 不在 v1，结构化降级"
    );
    assert!(backend.calls.lock().unwrap().is_empty());
    // 无交接能力：稳定拒绝（PL-011），同样不触碰后端。
    let mut backend = FakeBackend::new();
    let mut req = request(HeadersClass::RefererOnly, ProtocolKind::Dash);
    req.external_client_handoff = HandoffAvailability::Unavailable;
    let plan = plan_delivery(&req, &mut backend);
    assert_eq!(
        plan,
        DeliveryPlan::Rejected(CoreError::CapabilitiesUnavailable)
    );
    assert!(backend.calls.lock().unwrap().is_empty());
}

#[test]
fn downgrade_is_single_step_without_cycles() {
    let direct = DeliveryPlan::Direct {
        url: "https://cdn.example.com/v.mp4".to_string(),
    };
    // 运行中失败 → 单次降级为外部交接建议
    assert_eq!(
        downgrade_once(
            &direct,
            StartOutcome::Failed,
            false,
            HandoffAvailability::Available
        ),
        Some(DeliveryPlan::ExternalClientHandoff(
            ExternalClientHandoff::new(HandoffReason::StartFailed)
        ))
    );
    // 平台无交接能力 → 链结束，不产生建议（PL-011）
    assert_eq!(
        downgrade_once(
            &direct,
            StartOutcome::Failed,
            false,
            HandoffAvailability::Unavailable
        ),
        None
    );
    // 已降级过 → 不再降级（无循环）
    assert_eq!(
        downgrade_once(
            &direct,
            StartOutcome::Failed,
            true,
            HandoffAvailability::Available
        ),
        None
    );
    // 交接建议失败不再降级
    assert_eq!(
        downgrade_once(
            &DeliveryPlan::ExternalClientHandoff(ExternalClientHandoff::new(
                HandoffReason::StartFailed
            )),
            StartOutcome::Failed,
            false,
            HandoffAvailability::Available
        ),
        None
    );
    // 成功与拒绝不产生降级
    assert_eq!(
        downgrade_once(
            &direct,
            StartOutcome::Started,
            false,
            HandoffAvailability::Available
        ),
        None
    );
    assert_eq!(
        downgrade_once(
            &DeliveryPlan::Rejected(CoreError::DrmProtected),
            StartOutcome::Failed,
            false,
            HandoffAvailability::Available
        ),
        None
    );
}

#[test]
fn e2e_004_fake_handoff_creates_no_session_material() {
    // 广告连续性未知 + 从头播放 → 外部客户端交接建议（PL-009/E2E-004 fake）。
    let mut backend = FakeBackend::new();
    let mut req = request(HeadersClass::None, ProtocolKind::Hls);
    let candidate = MediaCandidate::new(
        "https://cdn.example.com/master.m3u8".to_string(),
        ProtocolKind::Hls,
        false,
        HeadersClass::None,
        None,
        None,
        AdContinuity::Unknown,
    );
    req.input = CastPolicyInput::new(
        req.input.page().clone(),
        PlaybackState::new(0.0, Some(3600.0), false),
        candidate,
        req.input.receiver(),
    );
    let plan = plan_delivery(&req, &mut backend);
    let DeliveryPlan::ExternalClientHandoff(advice) = plan else {
        panic!("应为外部交接建议: {plan:?}")
    };
    assert_eq!(advice.reason(), HandoffReason::AdContinuityUnknown);
    assert_eq!(advice.confirmation(), HandoffConfirmation::Required);
    // PL-015：交接不持有媒体 URL/Relay token/receiver session，也不创建任何
    // 会话——后端零调用；Debug 输出不含候选 URL。
    assert!(backend.calls.lock().unwrap().is_empty());
    assert!(!format!("{advice:?}").contains("cdn.example.com"));
    // 重复规划（含取消后重试、旧结果重放）是纯函数：结果一致且仍不触碰后端。
    assert_eq!(
        plan_delivery(&req, &mut backend),
        DeliveryPlan::ExternalClientHandoff(advice)
    );
    assert!(backend.calls.lock().unwrap().is_empty());
}

#[tokio::test]
async fn e2e_002_fake_relay_plan_produces_working_url() {
    // fake 设备链路：RelayRuntime + MockUpstream + 真 CoreSessionBackend
    let upstream = MockUpstream::start(vec![(
        "/movie.mp4".to_string(),
        UpstreamScript::RangeAware {
            content_type: Some("video/mp4".to_string()),
            body: b"0123456789".to_vec(),
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
    let mut backend = CoreSessionBackend::new(runtime.core().clone(), runtime.media_base_url());

    let mut req = request(HeadersClass::RefererOnly, ProtocolKind::Mp4);
    req.input = CastPolicyInput::new(
        req.input.page().clone(),
        req.input.playback(),
        MediaCandidate::new(
            upstream.url("/movie.mp4"),
            ProtocolKind::Mp4,
            false,
            HeadersClass::RefererOnly,
            None,
            None,
            AdContinuity::Preserved,
        ),
        req.input.receiver(),
    );
    req.receiver_ip = Some(std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST));

    let plan = plan_delivery(&req, &mut backend);
    let DeliveryPlan::Relay { media_url } = plan else {
        panic!("应为 Relay 计划: {plan:?}")
    };
    assert!(media_url.contains("/s/"), "{media_url}");

    // fake 接收端拉流：Range 正常（E2E-002 的 fake 覆盖）
    let resp = reqwest::Client::new()
        .get(&media_url)
        .header("Range", "bytes=0-3")
        .send()
        .await
        .unwrap();
    assert_eq!(resp.status(), 206);
    assert_eq!(resp.bytes().await.unwrap().as_ref(), b"0123");

    // token 失效（撤销触发器；服务仍在运行 → 401）
    runtime.trigger(crayon_relay::session::RevokeReason::Navigation, None);
    let resp = reqwest::Client::new().get(&media_url).send().await.unwrap();
    assert_eq!(resp.status(), 401);
    runtime.stop().await;
}
