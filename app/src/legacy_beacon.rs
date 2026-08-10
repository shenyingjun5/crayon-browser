//! legacy loopback Beacon 服务（127.0.0.1:8377）：注入脚本的兜底上报通道 + 前端验证回执。
//!
//! 本文件是 FND-07C 从 `main.rs` 逐字移出的 beacon 服务；route 集合、固定端口、
//! 返回字节与日志文案与迁移前完全一致。生命周期显式拆为两步：
//! `beacon_router` 构造路由（可测试），`start_beacon_server` 负责绑定与服务。
//!
//! 写入约定（锁所有权见 `runtime.rs`）：`/sniff` 与 `/probe-report` 是本服务对
//! `AppState` 的唯一写入点。

use crate::models::ProbeReport;
use crate::runtime::{push_hit, AppState};
use axum::{extract::Query, response::IntoResponse, routing::get, Router};
use std::collections::HashMap;
use std::sync::Arc;

/// beacon 路由：/sniff 命中上报、/diag 页态诊断、/probe-report 探针回传。
pub(crate) fn beacon_router(state: Arc<AppState>) -> Router {
    let st = state.clone();
    let st2 = state.clone();
    Router::new()
        .route(
            "/sniff",
            get(move |Query(q): Query<HashMap<String, String>>| {
                let st = st.clone();
                async move {
                    if let Some(data) = q.get("data") {
                        if let Ok(v) = serde_json::from_str::<serde_json::Value>(data) {
                            let u = v.get("url").and_then(|x| x.as_str()).unwrap_or("");
                            let p = v.get("page").and_then(|x| x.as_str()).unwrap_or("");
                            let proto = v.get("proto").and_then(|x| x.as_str()).map(str::to_string);
                            push_hit(&st.hits, u.to_string(), p.to_string(), proto);
                        }
                    }
                    // 1x1 gif
                    let gif: &[u8] = &[
                        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
                        0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
                    ];
                    ([(axum::http::header::CONTENT_TYPE, "image/gif")], gif).into_response()
                }
            }),
        )
        // 页态诊断：嗅探结束时回传标题/video 数/媒体资源清单（排查零命中站点）
        .route(
            "/diag",
            get(move |Query(q): Query<HashMap<String, String>>| async move {
                if let Some(msg) = q.get("msg") {
                    println!("[diag] {msg}");
                }
                axum::http::StatusCode::NO_CONTENT
            }),
        )
        // 解码探针回传：probeplayer 页抽帧统计（f=mean,std;mean,std）
        .route(
            "/probe-report",
            get(move |Query(q): Query<HashMap<String, String>>| {
                let st = st2.clone();
                async move {
                    if let Some(id) = q.get("id") {
                        let frames = q
                            .get("f")
                            .map(|f| {
                                f.split(';')
                                    .filter_map(|pair| {
                                        let mut it = pair.split(',');
                                        Some((
                                            it.next()?.parse::<f64>().ok()?,
                                            it.next()?.parse::<f64>().ok()?,
                                        ))
                                    })
                                    .collect::<Vec<_>>()
                            })
                            .unwrap_or_default();
                        let rep = ProbeReport {
                            frames,
                            err: q.get("err").cloned(),
                        };
                        println!(
                            "[probe] 收到回传 id={id} 帧数={} err={:?}",
                            rep.frames.len(),
                            rep.err
                        );
                        st.probe_reports.lock().unwrap().insert(id.clone(), rep);
                    }
                    ([(axum::http::header::CONTENT_TYPE, "image/gif")], &[][..]).into_response()
                }
            }),
        )
}

/// beacon 上报服务（127.0.0.1:8377）：注入脚本的兜底上报通道 + 前端验证回执。
pub(crate) async fn start_beacon_server(state: Arc<AppState>) {
    let app = beacon_router(state);
    if let Ok(listener) = tokio::net::TcpListener::bind("127.0.0.1:8377").await {
        println!("[beacon] 上报服务: http://127.0.0.1:8377/sniff");
        let _ = axum::serve(listener, app).await;
    }
}

#[cfg(test)]
#[path = "legacy_beacon_tests.rs"]
mod tests;
