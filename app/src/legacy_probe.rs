//! legacy 解码探针编排（FND-07D 从 `main.rs` 逐字移出）：隐藏 webview 加载
//! relay 的 /probeplayer 页真实解码抽帧，对「播放列表层面看不出问题」的候选
//! 做画面层兜底判定。行为、日志文案与超时口径与迁移前完全一致。

use crate::models::ProbeTarget;
use crate::runtime::AppState;
use std::sync::Arc;
use std::time::Duration;
use tauri::{AppHandle, Emitter, Manager, WebviewUrl, WebviewWindowBuilder};

/// 单流解码探针：隐藏 webview 加载 relay 的 /probeplayer 页（与 /proxy 同源，
/// canvas 可读），抽帧统计经 beacon 回传后综合判定。
/// 返回 Some(受限原因)=实测不可播（加扰黑屏 / 加载失败）；None=可播或无结论。
pub(crate) async fn probe_one(app: &AppHandle, relay_url: &str) -> Option<&'static str> {
    let state = app.state::<Arc<AppState>>();
    let id = format!(
        "p{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .ok()?
            .as_millis()
    );
    let page = format!(
        "{}/probeplayer?src={}&id={}&report={}",
        state.relay_base,
        crayon_browser_core::encode_url_component(relay_url),
        id,
        crayon_browser_core::encode_url_component("http://127.0.0.1:8377")
    );
    // 隐藏窗口须主线程创建（GTK 约束，macOS 同样保险）
    let label = format!("probe-{id}");
    let app2 = app.clone();
    let (tx, rx) = tokio::sync::oneshot::channel();
    app.run_on_main_thread(move || {
        let r =
            WebviewWindowBuilder::new(&app2, &label, WebviewUrl::External(page.parse().unwrap()))
                .title("probe")
                .visible(false)
                .build();
        let _ = tx.send(r);
    })
    .map_err(|e| format!("dispatch main thread: {e}"))
    .ok()?;
    let win = rx.await.ok()?.ok()?;

    // 轮询回传（probeplayer 正常 ~7s 上报，12s 自超时；这里 16s 兜底）
    let mut report = None;
    for _ in 0..80 {
        tokio::time::sleep(Duration::from_millis(200)).await;
        let r = state.probe_reports.lock().unwrap().remove(&id);
        if r.is_some() {
            report = r;
            break;
        }
    }
    let app3 = app.clone();
    let _ = app.run_on_main_thread(move || {
        let _ = win.close();
        let _ = app3;
    });

    let rep = report?;
    let stats: Vec<crayon_browser_core::probe::FrameStat> = rep
        .frames
        .iter()
        .map(|&(mean, std)| crayon_browser_core::probe::FrameStat { mean, std })
        .collect();
    // 超时不算加载失败（可能是网络慢），播放器 error 事件才算确定性失败
    let load_error = matches!(rep.err.as_deref(), Some(e) if e != "timeout");
    match crayon_browser_core::probe::probe_verdict(&stats, load_error) {
        crayon_browser_core::probe::ProbeVerdict::Scrambled => Some(crayon_browser_core::probe::SCRAMBLED_REASON),
        crayon_browser_core::probe::ProbeVerdict::LoadFailed => Some(crayon_browser_core::probe::LOAD_FAILED_REASON),
        crayon_browser_core::probe::ProbeVerdict::Playable => None,
        crayon_browser_core::probe::ProbeVerdict::Inconclusive => {
            println!("[probe] {relay_url} 无结论（err={:?}）", rep.err);
            None
        }
    }
}

/// 对全部可播候选逐个跑解码探针（顺序执行，避免并发拉流）。
/// 实测不可播的流：向前端发 `probe-restriction` 事件（UI 异步打标），
/// 并返回 (流地址, 受限原因) 集合（CLI 模式据此在打印前直接改结果）。
pub(crate) async fn probe_scrambled(
    app: &AppHandle,
    targets: &[ProbeTarget],
) -> Vec<(String, &'static str)> {
    let mut bad = Vec::new();
    for t in targets {
        match probe_one(app, &t.relay_url).await {
            Some(reason) => {
                println!("[probe] 实测不可播（{reason}）: {}", t.url);
                bad.push((t.url.clone(), reason));
                let _ = app.emit(
                    "probe-restriction",
                    serde_json::json!({
                        "url": t.url,
                        "reason": reason,
                    }),
                );
            }
            None => println!("[probe] 实测可播或无结论: {}", t.url),
        }
    }
    bad
}
