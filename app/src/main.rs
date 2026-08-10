//! `legacy-dev` Tauri 迁移壳（Tauri 2），不是正式产品构建目标。
//!
//! 架构：主窗口 UI（输入网址/展示结果/播放），叠加 L1/L3 提取（get-video Extractor）、
//! 隐藏 WebviewWindow（加载目标页、注入嗅探 JS，L2）、get-video relay
//! （0.0.0.0:8321，播放地址中转；局域网设备可访问投屏地址）。
//!
//! 嗅探结果上报双通道（去重合并）：
//!
//! 1. `window.__TAURI__.event.emit('sniff-found', ...)`（IPC，需 capabilities 放行 remote）；
//! 2. Image beacon → 本应用自建的 127.0.0.1:8377 上报服务（兜底，跨域无预检）。
//!
//! CLI 无头验证模式：
//! - `get-video-app --sniff-cli <url>`：跑 L2 嗅探，打印 SNIFF_RESULT_JSON 后退出；
//! - `get-video-app --extract-cli <url>`：跑 L1/L3 提取，打印 EXTRACT_RESULT_JSON 后退出。
//!
//! 模块划分（FND-07）：本文件只保留装配入口（relay 启动、状态构造、
//! 命令注册、CLI/UI-test 编排）；数据模型见 `models`，共享状态见 `runtime`，
//! 嗅探/探针/提取/登录编排见 `legacy_sniff`/`legacy_probe`/`commands`/`login`，
//! beacon 与网络地址见 `legacy_beacon`/`legacy_network`。

use get_video::relay::{self, RelayConfig};
use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tauri::{Listener, Manager};

mod commands;
mod legacy_beacon;
mod legacy_network;
mod legacy_probe;
mod legacy_sniff;
mod legacy_sniffer;
mod login;
mod models;
mod runtime;

use commands::{close_login, do_extract, extract, lan_addr, open_login, report_log, sniff};
use legacy_beacon::start_beacon_server;
use legacy_network::lan_ip;
use legacy_probe::probe_scrambled;
use legacy_sniff::do_sniff;
use models::{extract_probe_targets, sniff_probe_targets};
use runtime::AppState;

fn main() {
    // CLI 验证模式：--sniff-cli <url>（无头直接跑 L2 嗅探）
    // CLI 验证模式：--extract-cli <url>（无头直接跑 L1/L3 提取）
    // UI 验证模式：--ui-test <url>（GUI 起来后在主窗口里真实点击「嗅探」走完整 IPC 链路）
    let args: Vec<String> = std::env::args().collect();
    let cli_url = args
        .windows(2)
        .find(|w| w[0] == "--sniff-cli")
        .map(|w| w[1].clone());
    let extract_cli_url = args
        .windows(2)
        .find(|w| w[0] == "--extract-cli")
        .map(|w| w[1].clone());
    let ui_test_url = args
        .windows(2)
        .find(|w| w[0] == "--ui-test")
        .map(|w| w[1].clone());

    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            sniff,
            extract,
            report_log,
            open_login,
            close_login,
            lan_addr
        ])
        .setup(move |app| {
            // 前端渲染回执（无头验证 UI 链路用）
            app.listen_any("ui-results", |ev| {
                println!("[ui] 前端已渲染结果: {}", ev.payload());
            });
            app.listen_any("ui-ready", |_| {
                println!("[ui] 前端页面已加载");
            });
            app.listen_any("ui-probe", |ev| {
                println!("[probe] {}", ev.payload());
            });
            // 启动 get-video relay：绑定 0.0.0.0 让局域网设备（手机/电视投屏）可访问；
            // 本机播放仍走 127.0.0.1（8321 被占则退回随机端口）
            let dash_store: get_video::relay::DashStore = Default::default();
            let ds1 = dash_store.clone();
            let ds2 = dash_store.clone();
            let handle = tauri::async_runtime::block_on(async {
                match relay::start(RelayConfig {
                    host: "0.0.0.0".into(),
                    port: 8321,
                    allow_private_hosts: false,
                    rules_path: None,
                    dash_store: Some(ds1),
                })
                .await
                {
                    Ok(h) => h,
                    Err(e) => {
                        eprintln!("[relay] 8321 绑定失败（{e}），退回随机端口");
                        relay::start(RelayConfig {
                            host: "0.0.0.0".into(),
                            port: 0,
                            allow_private_hosts: false,
                            rules_path: None,
                            dash_store: Some(ds2),
                        })
                        .await
                        .expect("relay 启动失败")
                    }
                }
            });
            let port = handle.addr.port();
            let base = format!("http://127.0.0.1:{port}");
            let lan_base = match lan_ip() {
                Some(ip) => format!("http://{ip}:{port}"),
                None => base.clone(),
            };
            println!("[relay] 已启动: {base}（局域网: {lan_base}）");
            let state = Arc::new(AppState {
                hits: Mutex::new(Vec::new()),
                relay_base: base,
                lan_base,
                dash_store,
                probe_reports: Mutex::new(HashMap::new()),
                busy: AtomicBool::new(false),
                _relay: Mutex::new(Some(handle)),
            });
            app.manage(state.clone());
            tauri::async_runtime::spawn(start_beacon_server(state.clone()));

            // CLI 模式：隐藏主窗口，直接跑嗅探并打印结果
            if let Some(u) = cli_url {
                if let Some(w) = app.get_webview_window("main") {
                    let _ = w.hide();
                }
                let ah = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    let r = do_sniff(&ah, &u).await;
                    match r {
                        Ok(mut resp) => {
                            // CLI 同步跑解码探针，打印前直接把异常流标受限
                            let targets = sniff_probe_targets(&resp);
                            let bad = probe_scrambled(&ah, &targets).await;
                            for item in resp.results.iter_mut() {
                                if let Some((_, reason)) =
                                    bad.iter().find(|(url, _)| url == &item.url)
                                {
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
            if let Some(u) = extract_cli_url {
                if let Some(w) = app.get_webview_window("main") {
                    let _ = w.hide();
                }
                let relay_base = state.relay_base.clone();
                let ah = app.handle().clone();
                let ah2 = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    let r = do_extract(&ah2, &relay_base, &u).await;
                    match r {
                        Ok(mut info) => {
                            // CLI 同步跑解码探针（同 sniff CLI）
                            let targets = extract_probe_targets(&info);
                            let bad = probe_scrambled(&ah, &targets).await;
                            for f in info.formats.iter_mut() {
                                if let Some((_, reason)) = bad.iter().find(|(url, _)| url == &f.url)
                                {
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
            if let Some(u) = ui_test_url {
                let ah = app.handle().clone();
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
                let ah = app.handle().clone();
                tauri::async_runtime::spawn(async move {
                    tokio::time::sleep(Duration::from_secs(3)).await;
                    if let (Some(w), Some(js)) = (ah.get_webview_window("main"), custom) {
                        let _ = w.eval(&js);
                    }
                    tokio::time::sleep(Duration::from_secs(45)).await;
                    ah.exit(0);
                });
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
