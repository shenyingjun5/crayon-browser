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
