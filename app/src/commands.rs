//! legacy Tauri 命令面与 L1/L3 提取编排（FND-07D 从 `main.rs` 逐字移出）。
//!
//! handler 名称、参数、序列化输出与日志文案与迁移前完全一致；
//! 防重入（`busy`）与后台探针调度语义不变。

use crate::legacy_probe::probe_scrambled;
use crate::legacy_sniff::do_sniff;
use crate::login::site_cookie_header;
use crate::models::{extract_probe_targets, sniff_probe_targets, SniffResponse};
use crate::runtime::AppState;
use crayon_browser_core::extract::{Extractor, RulePack, VideoInfo};
use std::sync::atomic::Ordering;
use std::sync::Arc;
use tauri::{AppHandle, Manager};

#[tauri::command]
pub(crate) fn report_log(msg: String) {
    println!("[page] {msg}");
}

#[tauri::command]
pub(crate) fn lan_addr(app: AppHandle) -> String {
    app.state::<Arc<AppState>>().lan_base.clone()
}

#[tauri::command]
pub(crate) async fn sniff(app: AppHandle, url: String) -> Result<SniffResponse, String> {
    let state = app.state::<Arc<AppState>>();
    if state.busy.swap(true, Ordering::SeqCst) {
        return Err("已有任务进行中".to_string());
    }
    println!("[sniff] IPC 调用: {url}");
    let r = do_sniff(&app, &url).await;
    state.busy.store(false, Ordering::SeqCst);
    if let Ok(resp) = &r {
        println!("[sniff] 返回 {} 条结果给前端", resp.count);
        // 后台解码探针：全部可播候选逐个实测画面，异常者经事件异步打标
        let targets = sniff_probe_targets(resp);
        if !targets.is_empty() {
            let ah = app.clone();
            tauri::async_runtime::spawn(async move {
                probe_scrambled(&ah, &targets).await;
            });
        }
    }
    if let Err(e) = &r {
        println!("[sniff] 失败: {e}");
    }
    r
}

#[tauri::command]
pub(crate) async fn extract(app: AppHandle, url: String) -> Result<VideoInfo, String> {
    let state = app.state::<Arc<AppState>>();
    if state.busy.swap(true, Ordering::SeqCst) {
        return Err("已有任务进行中".to_string());
    }
    println!("[extract] IPC 调用: {url}");
    let relay_base = state.relay_base.clone();
    let r = do_extract(&app, &relay_base, &url).await;
    state.busy.store(false, Ordering::SeqCst);
    if let Ok(info) = &r {
        println!(
            "[extract] 返回 {} 个格式给前端（source={}）",
            info.formats.len(),
            info.source
        );
        // 后台解码探针：同嗅探链路
        let targets = extract_probe_targets(info);
        if !targets.is_empty() {
            let ah = app.clone();
            tauri::async_runtime::spawn(async move {
                probe_scrambled(&ah, &targets).await;
            });
        }
    }
    if let Err(e) = &r {
        println!("[extract] 失败: {e}");
    }
    r
}

/// 加载 L3 规则包：环境变量 CRAYON_LEGACY_RULES 指向本地 JSON；未设置/加载失败用空包。
fn load_rule_pack() -> RulePack {
    match std::env::var("CRAYON_LEGACY_RULES") {
        Ok(p) if !p.is_empty() => match RulePack::load(std::path::Path::new(&p)) {
            Ok(rp) => {
                println!("[extract] 规则包已加载: {p}");
                rp
            }
            Err(e) => {
                eprintln!("[extract] 规则包加载失败（{e}），使用空规则包");
                RulePack::empty()
            }
        },
        _ => RulePack::empty(),
    }
}

/// L1/L3 提取：静态解析 + 站点专用解析器 + 规则包 + DRM 检测（秒级，无 webview）。
pub(crate) async fn do_extract(
    app: &AppHandle,
    relay_base: &str,
    url: &str,
) -> Result<VideoInfo, String> {
    let mut extractor = Extractor::new(relay_base, load_rule_pack());
    extractor.set_dash_store(app.state::<Arc<AppState>>().dash_store.clone());
    // webview 登录态（B 站等）→ 传给 L3 站点解析器解锁高清晰度
    let cookie = site_cookie_header(app, url);
    if cookie.is_some() {
        println!("[extract] 携带站点登录 Cookie");
    }
    extractor.extract_with_cookie(url, cookie.as_deref()).await
}
