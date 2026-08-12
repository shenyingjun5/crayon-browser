//! legacy 运行时状态所有权：`AppState` 是全部共享集合与锁的唯一持有者。
//!
//! 本文件是 FND-07B 从 `main.rs` 逐字移出的 `AppState` 与去重写入 `push_hit`；
//! 字段、日志文案与同步语义与迁移前完全一致。
//!
//! 锁所有权约定（只读引用方不得反向修改）：
//! - `hits`：beacon 服务（`/sniff` 上报）写入，`do_sniff` 每轮 drain 合并；
//! - `probe_reports`：beacon 服务（`/probe-report`）写入，`probe_one` 轮询取出；
//! - `_relay`：启动时装配后只读持有，保证 relay 生命周期与 app 一致；
//! - `busy`：sniff/extract 命令的防重入标志（同时只跑一个任务）。

use crate::models::{ProbeReport, SniffHit};
use crayon_browser_core::relay::RelayHandle;
use std::collections::HashMap;
use std::sync::atomic::AtomicBool;
use std::sync::Mutex;

/// 共享状态：嗅探命中收集 + relay 基地址 + 防重入锁。
pub(crate) struct AppState {
    pub(crate) hits: Mutex<Vec<SniffHit>>,
    pub(crate) relay_base: String,
    /// 局域网可访问的 relay 基地址（投屏给手机/电视用），如 `http://192.168.1.8:8321`。
    pub(crate) lan_base: String,
    /// 与 relay 共享的 DASH MPD 仓库（提取器写入，/dashmpd/{id} 读出）。
    pub(crate) dash_store: crayon_browser_core::relay::DashStore,
    /// 解码探针回传（probeplayer 页 → /probe-report beacon）。
    pub(crate) probe_reports: Mutex<HashMap<String, ProbeReport>>,
    pub(crate) busy: AtomicBool,
    pub(crate) _relay: Mutex<Option<RelayHandle>>,
}

pub(crate) fn push_hit(
    hits: &Mutex<Vec<SniffHit>>,
    url: String,
    page: String,
    proto: Option<String>,
) {
    if url.is_empty() {
        return;
    }
    let mut g = hits.lock().unwrap();
    if !g.iter().any(|h| h.url == url) {
        println!("[sniff] 命中: {url}");
        g.push(SniffHit { url, page, proto });
    }
}

#[cfg(test)]
#[path = "runtime_tests.rs"]
mod tests;
