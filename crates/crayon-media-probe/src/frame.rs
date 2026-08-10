//! Pure decoded-frame statistics and bounded probe verdicts.

/// 单帧灰度统计（0-255）。
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct FrameStat {
    pub mean: f64,
    pub std: f64,
}

const DEGENERATE_MEAN_MAX: f64 = 8.0;
const DEGENERATE_STD_MAX: f64 = 5.0;

/// 全部采样帧都「退化」（纯黑或近纯色）才判异常。
///
/// 要求全部帧一致退化，避免把正常的黑场片头/转场误判为加扰；
/// 空样本（没采到帧）不做结论。
pub fn frames_degenerate(stats: &[FrameStat]) -> bool {
    !stats.is_empty()
        && stats
            .iter()
            .all(|stat| stat.mean < DEGENERATE_MEAN_MAX || stat.std < DEGENERATE_STD_MAX)
}

/// 探针综合结论。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProbeVerdict {
    /// 采到帧且画面正常。
    Playable,
    /// 采到帧且全部平坦（纯黑/纯色）——私有加扰。
    Scrambled,
    /// 播放器报确定性错误（变体 404、被拒绝等），一帧没采到。
    LoadFailed,
    /// 超时/无数据——不下结论（宁漏不误）。
    Inconclusive,
}

/// 探针综合判定：`load_error` 为播放器 error 事件（不含超时）。
pub fn probe_verdict(frames: &[FrameStat], load_error: bool) -> ProbeVerdict {
    if !frames.is_empty() {
        return if frames_degenerate(frames) {
            ProbeVerdict::Scrambled
        } else {
            ProbeVerdict::Playable
        };
    }
    if load_error {
        return ProbeVerdict::LoadFailed;
    }
    ProbeVerdict::Inconclusive
}

#[cfg(test)]
#[path = "frame_tests.rs"]
mod tests;
