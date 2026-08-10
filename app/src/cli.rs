//! legacy CLI/UI-test 编排（FND-07E 从 `main.rs` 逐字移出）。
//!
//! 无头验证模式：`--sniff-cli <url>`（L2 嗅探，打印 SNIFF_RESULT_JSON 后退出）、
//! `--extract-cli <url>`（L1/L3 提取，打印 EXTRACT_RESULT_JSON 后退出）、
//! `--ui-test <url>`（GUI 内触发解析并读取播放器状态）、`--probe-eval <js>`
//! （主窗口执行任意 JS）。参数解析、CLI marker 与等待时长与迁移前完全一致。

use crate::commands::do_extract;
use crate::legacy_probe::probe_scrambled;
use crate::legacy_sniff::do_sniff;
use crate::models::{extract_probe_targets, sniff_probe_targets};
use crate::runtime::AppState;
use std::sync::Arc;
use std::time::Duration;
use tauri::{AppHandle, Manager};

/// CLI 验证模式参数（均为「标志 + URL」形式；未出现为 None）。
pub(crate) struct CliModes {
    pub(crate) sniff: Option<String>,
    pub(crate) extract: Option<String>,
    pub(crate) ui_test: Option<String>,
}

pub(crate) fn parse_cli_modes() -> CliModes {
    let args: Vec<String> = std::env::args().collect();
    let sniff = args
        .windows(2)
        .find(|w| w[0] == "--sniff-cli")
        .map(|w| w[1].clone());
    let extract = args
        .windows(2)
        .find(|w| w[0] == "--extract-cli")
        .map(|w| w[1].clone());
    let ui_test = args
        .windows(2)
        .find(|w| w[0] == "--ui-test")
        .map(|w| w[1].clone());
    CliModes {
        sniff,
        extract,
        ui_test,
    }
}

/// 在 setup 中调度 CLI/UI-test/诊断模式（全部 spawn 后台任务，不阻塞 setup）。
pub(crate) fn run_cli_modes(app: &AppHandle, state: &Arc<AppState>, modes: CliModes) {
    // CLI 模式：隐藏主窗口，直接跑嗅探并打印结果
    if let Some(u) = modes.sniff {
        if let Some(w) = app.get_webview_window("main") {
            let _ = w.hide();
        }
        let ah = app.clone();
        tauri::async_runtime::spawn(async move {
            let r = do_sniff(&ah, &u).await;
            match r {
                Ok(mut resp) => {
                    // CLI 同步跑解码探针，打印前直接把异常流标受限
                    let targets = sniff_probe_targets(&resp);
                    let bad = probe_scrambled(&ah, &targets).await;
                    for item in resp.results.iter_mut() {
                        if let Some((_, reason)) = bad.iter().find(|(url, _)| url == &item.url) {
                            item.restriction = Some(reason.to_string());
                            item.relay_url = None;
                        }
                    }
                    println!(
                        "SNIFF_RESULT_JSON: {}",
                        serde_json::to_string(&resp).unwrap()
                    );
                }
                Err(e) => println!("SNIFF_RESULT_JSON: {{\"error\": \"{e}\"}}"),
            }
            ah.exit(0);
        });
    }

    // CLI 模式：隐藏主窗口，直接跑 L1/L3 提取并打印结果
    if let Some(u) = modes.extract {
        if let Some(w) = app.get_webview_window("main") {
            let _ = w.hide();
        }
        let relay_base = state.relay_base.clone();
        let ah = app.clone();
        let ah2 = app.clone();
        tauri::async_runtime::spawn(async move {
            let r = do_extract(&ah2, &relay_base, &u).await;
            match r {
                Ok(mut info) => {
                    // CLI 同步跑解码探针（同 sniff CLI）
                    let targets = extract_probe_targets(&info);
                    let bad = probe_scrambled(&ah, &targets).await;
                    for f in info.formats.iter_mut() {
                        if let Some((_, reason)) = bad.iter().find(|(url, _)| url == &f.url) {
                            f.restriction = Some(reason.to_string());
                            f.relay_url = None;
                        }
                    }
                    println!(
                        "EXTRACT_RESULT_JSON: {}",
                        serde_json::to_string(&info).unwrap()
                    );
                }
                Err(e) => println!("EXTRACT_RESULT_JSON: {{\"error\": \"{e}\"}}"),
            }
            ah.exit(0);
        });
    }

    // legacy UI 验证模式：填地址并触发解析，只读取结果/播放器状态；
    // 禁止替用户点击媒体或启动播放。
    if let Some(u) = modes.ui_test {
        let ah = app.clone();
        tauri::async_runtime::spawn(async move {
            tokio::time::sleep(Duration::from_secs(3)).await;
            if let Some(w) = ah.get_webview_window("main") {
                let js = format!(
                    "document.getElementById('url').value = {}; go();",
                    serde_json::to_string(&u).unwrap()
                );
                println!("[ui-test] 主窗口注入点击嗅探: {u}");
                match w.eval(&js) {
                    Ok(()) => println!("[ui-test] eval ok"),
                    Err(e) => println!("[ui-test] eval 失败: {e}"),
                }
                // 统一解析链路：快速提取 + 深度嗅探最长约 20s，只等待结果渲染。
                tokio::time::sleep(Duration::from_secs(19)).await;
                let _ = w.eval(
                    "try{const v=document.getElementById('player');window.__TAURI__.core.invoke('report_log',{msg:'播放器状态: currentTime='+v.currentTime.toFixed(2)+' paused='+v.paused+' error='+(v.error&&v.error.message||'none')+' readyState='+v.readyState+' networkState='+v.networkState});}catch(e){window.__TAURI__.core.invoke('report_log',{msg:'状态读取失败: '+e});}",
                );
            }
            tokio::time::sleep(Duration::from_secs(4)).await;
            ah.exit(0);
        });
    }

    // 诊断模式：--probe-eval <js>，在主窗口执行任意 JS（可用 report_log 回传）
    if let Some(pos) = std::env::args().position(|a| a == "--probe-eval") {
        let custom = std::env::args().nth(pos + 1);
        let ah = app.clone();
        tauri::async_runtime::spawn(async move {
            tokio::time::sleep(Duration::from_secs(3)).await;
            if let (Some(w), Some(js)) = (ah.get_webview_window("main"), custom) {
                let _ = w.eval(&js);
            }
            tokio::time::sleep(Duration::from_secs(45)).await;
            ah.exit(0);
        });
    }
}
