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
//! 模块划分（FND-07）：本文件只保留命令注册与装配入口；装配见 `app`，
//! CLI/UI-test 编排见 `cli`，relay 启动见 `legacy_relay`，数据模型见 `models`，
//! 共享状态见 `runtime`，嗅探/探针/提取/登录编排见
//! `legacy_sniff`/`legacy_probe`/`commands`/`login`，
//! beacon 与网络地址见 `legacy_beacon`/`legacy_network`。

mod app;
mod cli;
mod commands;
mod legacy_beacon;
mod legacy_network;
mod legacy_probe;
mod legacy_relay;
mod legacy_sniff;
mod legacy_sniffer;
mod login;
mod models;
mod runtime;

use commands::{close_login, extract, lan_addr, open_login, report_log, sniff};

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            sniff,
            extract,
            report_log,
            open_login,
            close_login,
            lan_addr
        ])
        .setup(app::setup)
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
