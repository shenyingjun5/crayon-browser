//! legacy 嗅探/探针数据模型：纯数据结构定义，不持有锁、不含行为。
//!
//! 本文件是 FND-07B 从 `main.rs` 逐字移出的 `Sniff*`/`Probe*` 类型；
//! 字段、序列化属性与 doc 注释与迁移前完全一致（字段集合经机器比对）。

use serde::Serialize;

#[derive(Debug, Clone)]
pub(crate) struct SniffHit {
    pub(crate) url: String,
    pub(crate) page: String,
    /// 内容判定协议提示（如响应体为 m3u8 但 URL 无扩展名时 = Some("hls")）。
    pub(crate) proto: Option<String>,
}

#[derive(Debug, Serialize)]
pub(crate) struct SniffResultItem {
    pub(crate) index: usize,
    pub(crate) url: String,
    pub(crate) protocol: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) quality: Option<String>,
    pub(crate) drm: bool,
    /// 受限原因（WASM 私有加扰 / 全站 DRM）：命中即不可播。
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) restriction: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) relay_url: Option<String>,
    /// 编码/封装标签（如 `H.264+AAC · TS`），供投屏接收端判断兼容性。
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) codec: Option<String>,
}

#[derive(Debug, Serialize)]
pub(crate) struct SniffResponse {
    pub(crate) page: String,
    pub(crate) count: usize,
    pub(crate) results: Vec<SniffResultItem>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(crate) note: Option<String>,
}

/// 探针回传：抽帧统计（mean/std 序列）+ 播放进度 + 可选错误。
#[derive(Debug, Clone, Default)]
pub(crate) struct ProbeReport {
    /// (mean, std) 采样序列。
    pub(crate) frames: Vec<(f64, f64)>,
    pub(crate) err: Option<String>,
}

/// 解码探针目标：候选流地址 + 它的 relay 地址。
pub(crate) struct ProbeTarget {
    pub(crate) url: String,
    pub(crate) relay_url: String,
}

/// 嗅探结果中需要跑解码探针的候选（未受限、非 DRM、原生可播协议，
/// 且编码是 webview 能解码的——HEVC/AV1 等跳过，避免「没画面 ≠ 流坏」误判）。
pub(crate) fn sniff_probe_targets(resp: &SniffResponse) -> Vec<ProbeTarget> {
    resp.results
        .iter()
        .filter(|r| {
            r.restriction.is_none()
                && !r.drm
                && (r.protocol == "hls" || r.protocol == "mp4")
                && crayon_browser_core::probe::webview_can_judge(r.codec.as_deref())
        })
        .filter_map(|r| {
            r.relay_url.clone().map(|relay_url| ProbeTarget {
                url: r.url.clone(),
                relay_url,
            })
        })
        .collect()
}

/// 提取结果中需要跑解码探针的候选（同嗅探链路的筛选口径）。
pub(crate) fn extract_probe_targets(info: &crayon_browser_core::extract::VideoInfo) -> Vec<ProbeTarget> {
    info.formats
        .iter()
        .filter(|f| {
            f.restriction.is_none()
                && !f.drm
                && (f.protocol == "hls" || f.protocol == "mp4")
                && crayon_browser_core::probe::webview_can_judge(f.codec.as_deref())
        })
        .filter_map(|f| {
            f.relay_url.clone().map(|relay_url| ProbeTarget {
                url: f.url.clone(),
                relay_url,
            })
        })
        .collect()
}

#[cfg(test)]
#[path = "models_tests.rs"]
mod tests;
