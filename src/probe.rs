//! 流可播性探针判定：对「播放列表层面看不出问题、但解码画面异常」的
//! 私有加扰流（央视频直播 WASM 加扰是唯一已知案例），唯一可靠的判定是
//! 真实解码后看画面。本模块只有判定逻辑（纯函数，可单测）；
//! 抽帧执行在 app 层（隐藏 webview 加载 relay /probeplayer 页）。
//!
//! 背景（2026-08-02 实证）：加扰流的 TS 容器、PAT/PMT、SPS/PPS、
//! slice 头 Exp-Golomb 全部合法，与干净流逐项一致——码流结构分析无法
//! 区分；实测画面为纯黑（mean=0, std=0）而正常内容为 mean 70-150、
//! std 39-52。

/// 单帧灰度统计（0-255）。
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct FrameStat {
    pub mean: f64,
    pub std: f64,
}

/// 全部采样帧都「退化」（纯黑或近纯色）才判异常。
///
/// 要求全部帧一致退化，避免把正常的黑场片头/转场误判为加扰；
/// 空样本（没采到帧）不做结论。
pub fn frames_degenerate(stats: &[FrameStat]) -> bool {
    !stats.is_empty() && stats.iter().all(|s| s.mean < 8.0 || s.std < 5.0)
}

/// 探针结论的中文原因文案（UI 标注与日志共用）。
pub const SCRAMBLED_REASON: &str = "WASM 私有加扰，实测解码画面异常，无法播放";
/// 流加载失败（404/拒绝等确定性错误）的原因文案。
pub const LOAD_FAILED_REASON: &str = "流地址失效或加载失败，无法播放";

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
mod tests {
    use super::*;

    #[test]
    fn pure_black_frames_are_degenerate() {
        let stats = [
            FrameStat {
                mean: 0.0,
                std: 0.0,
            },
            FrameStat {
                mean: 0.1,
                std: 0.0,
            },
        ];
        assert!(frames_degenerate(&stats));
    }

    #[test]
    fn normal_frames_not_degenerate() {
        // 央视纪录片实测值
        let stats = [
            FrameStat {
                mean: 70.3,
                std: 52.1,
            },
            FrameStat {
                mean: 150.0,
                std: 50.4,
            },
            FrameStat {
                mean: 147.5,
                std: 39.2,
            },
        ];
        assert!(!frames_degenerate(&stats));
    }

    #[test]
    fn black_opening_then_normal_not_degenerate() {
        // 片头黑场是正常内容：只要有一帧正常就不判
        let stats = [
            FrameStat {
                mean: 0.5,
                std: 0.2,
            },
            FrameStat {
                mean: 120.0,
                std: 45.0,
            },
        ];
        assert!(!frames_degenerate(&stats));
    }

    #[test]
    fn empty_sample_no_verdict() {
        assert!(!frames_degenerate(&[]));
    }

    #[test]
    fn flat_gray_frames_are_degenerate() {
        let stats = [FrameStat {
            mean: 128.0,
            std: 1.2,
        }];
        assert!(frames_degenerate(&stats));
    }

    #[test]
    fn verdict_load_failure() {
        // 变体 404 → 播放器报错、零帧 → 受限（4K 专区老片实测场景）
        assert_eq!(probe_verdict(&[], true), ProbeVerdict::LoadFailed);
    }

    #[test]
    fn verdict_timeout_inconclusive() {
        // 超时零帧 → 不下结论
        assert_eq!(probe_verdict(&[], false), ProbeVerdict::Inconclusive);
    }

    #[test]
    fn verdict_scrambled_and_playable() {
        let black = [FrameStat {
            mean: 0.0,
            std: 0.0,
        }];
        assert_eq!(probe_verdict(&black, false), ProbeVerdict::Scrambled);
        let normal = [FrameStat {
            mean: 70.3,
            std: 52.1,
        }];
        assert_eq!(probe_verdict(&normal, false), ProbeVerdict::Playable);
        // 有帧时忽略 error 标志（以画面为准）
        assert_eq!(probe_verdict(&normal, true), ProbeVerdict::Playable);
    }
}
