//! Beacon Router 契约与 query 边界测试：route 集合、状态码、返回字节、
//! 参数解析与状态写入边界。全部走内存 Router（tower oneshot），不绑定端口。

use super::*;
use axum::body::{to_bytes, Body};
use axum::http::{Request, StatusCode};
use std::sync::atomic::AtomicBool;
use std::sync::Mutex;
use tower::ServiceExt;

/// 与迁移前 `main.rs` 内联字节逐字一致的 1x1 gif（/sniff 返回字节锁定）。
const EXPECTED_GIF: &[u8] = &[
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
];

fn test_state() -> Arc<AppState> {
    Arc::new(AppState {
        hits: Mutex::new(Vec::new()),
        relay_base: "http://127.0.0.1:8321".to_string(),
        lan_base: "http://127.0.0.1:8321".to_string(),
        dash_store: Default::default(),
        probe_reports: Mutex::new(HashMap::new()),
        busy: AtomicBool::new(false),
        _relay: Mutex::new(None),
    })
}

/// 最小 application/x-www-form-urlencoded 值编码（Query 反序列化依赖）。
fn form_enc(s: &str) -> String {
    s.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                (b as char).to_string()
            }
            _ => format!("%{b:02X}"),
        })
        .collect()
}

async fn get(router: Router, uri: &str) -> (StatusCode, Option<String>, Vec<u8>) {
    let resp = router
        .oneshot(Request::builder().uri(uri).body(Body::empty()).unwrap())
        .await
        .unwrap();
    let status = resp.status();
    let content_type = resp
        .headers()
        .get(axum::http::header::CONTENT_TYPE)
        .and_then(|v| v.to_str().ok())
        .map(str::to_string);
    let body = to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap()
        .to_vec();
    (status, content_type, body)
}

#[tokio::test]
async fn route_contract_and_gif_bytes() {
    let state = test_state();
    let router = beacon_router(state.clone());

    // /diag：页态诊断回执，恒 204 空 body
    let (status, _, body) = get(router.clone(), "/diag?msg=hello").await;
    assert_eq!(status, StatusCode::NO_CONTENT);
    assert!(body.is_empty());

    // /sniff 缺 data 参数：仍回 1x1 gif，不记录命中
    let (status, content_type, body) = get(router.clone(), "/sniff").await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(content_type.as_deref(), Some("image/gif"));
    assert_eq!(body, EXPECTED_GIF);
    assert!(state.hits.lock().unwrap().is_empty());

    // /probe-report 缺 id：回 200 空 body（gif content-type），不写回传
    let (status, content_type, body) = get(router.clone(), "/probe-report").await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(content_type.as_deref(), Some("image/gif"));
    assert!(body.is_empty());
    assert!(state.probe_reports.lock().unwrap().is_empty());

    // route 集合边界：未知路径 404
    let (status, _, _) = get(router.clone(), "/unknown").await;
    assert_eq!(status, StatusCode::NOT_FOUND);

    // 方法边界：POST /sniff 405
    let resp = router
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/sniff")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::METHOD_NOT_ALLOWED);
}

#[tokio::test]
async fn sniff_route_records_and_dedups_hits() {
    let state = test_state();
    let router = beacon_router(state.clone());

    let data = form_enc(
        r#"{"url":"https://cdn.example.com/a.m3u8","page":"https://example.com/","proto":"hls"}"#,
    );
    let (status, _, _) = get(router.clone(), &format!("/sniff?data={data}")).await;
    assert_eq!(status, StatusCode::OK);
    {
        let hits = state.hits.lock().unwrap();
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].url, "https://cdn.example.com/a.m3u8");
        assert_eq!(hits[0].page, "https://example.com/");
        assert_eq!(hits[0].proto.as_deref(), Some("hls"));
    }

    // 同 URL 重复上报（IPC + beacon 双通道常态）：不追加
    let _ = get(router.clone(), &format!("/sniff?data={data}")).await;
    assert_eq!(state.hits.lock().unwrap().len(), 1);

    // 非法 JSON / 空 url：不记录
    let bad = form_enc("not-json");
    let _ = get(router.clone(), &format!("/sniff?data={bad}")).await;
    let empty_url = form_enc(r#"{"url":"","page":"p"}"#);
    let _ = get(router.clone(), &format!("/sniff?data={empty_url}")).await;
    assert_eq!(state.hits.lock().unwrap().len(), 1);
}

#[tokio::test]
async fn probe_report_parses_frames_and_skips_malformed() {
    let state = test_state();
    let router = beacon_router(state.clone());

    let (status, _, _) = get(
        router.clone(),
        "/probe-report?id=p1&f=10.5,2.5;0,0&err=timeout",
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    {
        let reports = state.probe_reports.lock().unwrap();
        let rep = reports.get("p1").unwrap();
        assert_eq!(rep.frames, vec![(10.5, 2.5), (0.0, 0.0)]);
        assert_eq!(rep.err.as_deref(), Some("timeout"));
    }

    // 畸形 pair 跳过、空段跳过；缺 err 为 None
    let _ = get(router.clone(), "/probe-report?id=p2&f=abc;;1,2").await;
    {
        let reports = state.probe_reports.lock().unwrap();
        let rep = reports.get("p2").unwrap();
        assert_eq!(rep.frames, vec![(1.0, 2.0)]);
        assert!(rep.err.is_none());
    }

    // 缺 f：frames 为空（不视为加载失败，由探针判定层处理）
    let _ = get(router.clone(), "/probe-report?id=p3&err=boom").await;
    {
        let reports = state.probe_reports.lock().unwrap();
        let rep = reports.get("p3").unwrap();
        assert!(rep.frames.is_empty());
        assert_eq!(rep.err.as_deref(), Some("boom"));
    }
}
