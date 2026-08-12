//! legacy 应用装配（FND-07E 从 `main.rs` 逐字移出）：前端事件回执监听、
//! relay 启动、共享状态构造、beacon spawn 与 CLI/UI-test 调度。
//! 装配顺序与日志文案与迁移前完全一致。

use crate::cli;
use crate::legacy_beacon::start_beacon_server;
use crate::legacy_relay::start_legacy_relay;
use crate::runtime::AppState;
use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use tauri::{Listener, Manager};

/// Tauri setup 入口：注册事件回执、启动 relay 与 beacon、构造共享状态、调度 CLI 模式。
pub(crate) fn setup(app: &mut tauri::App) -> Result<(), Box<dyn std::error::Error>> {
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
    // 启动 Crayon legacy relay：绑定 0.0.0.0 让局域网设备（手机/电视投屏）可访问；
    // 本机播放仍走 127.0.0.1（8321 被占则退回随机端口）
    let relay = start_legacy_relay();
    let state = Arc::new(AppState {
        hits: Mutex::new(Vec::new()),
        relay_base: relay.base,
        lan_base: relay.lan_base,
        dash_store: relay.dash_store,
        probe_reports: Mutex::new(HashMap::new()),
        busy: AtomicBool::new(false),
        _relay: Mutex::new(Some(relay.handle)),
    });
    app.manage(state.clone());
    tauri::async_runtime::spawn(start_beacon_server(state.clone()));

    cli::run_cli_modes(app.handle(), &state, cli::parse_cli_modes());
    Ok(())
}
