//! Ranking contract (MED-03): BR-006 visibility+recency choice, stable
//! tie-breaking, and background-audio handling.

use crayon_domain::TabId;
use crayon_media_observer::candidate::{rank, CandidateId, CandidateStore, RankingSignals};
use crayon_media_observer::{FrameContext, NavigationId, ObservationSource, SourceObservation};

/// Ingests N dummy URLs and returns their candidate ids in creation order.
fn make_ids(urls: &[&str]) -> Vec<CandidateId> {
    let mut store = CandidateStore::new();
    urls.iter()
        .map(|url| {
            store
                .ingest(
                    &SourceObservation::new(
                        TabId::new("tab-01").unwrap(),
                        NavigationId::INITIAL,
                        FrameContext::Main,
                        ObservationSource::NetworkRequest,
                        url.to_string(),
                        "https://example.com/watch".to_string(),
                        100,
                    )
                    .unwrap(),
                )
                .unwrap()
        })
        .collect()
}

#[test]
fn br_006_visible_and_recently_used_media_wins() {
    let ids = make_ids(&[
        "https://cdn.example.com/a.m3u8",
        "https://cdn.example.com/b.m3u8",
    ]);
    // 两个视频同时播放：a 不可见，b 可见面积最大且与 play 事件相邻。
    let ranked = rank(&[
        (ids[0], RankingSignals::new(false, false, true, true, 0)),
        (
            ids[1],
            RankingSignals::new(false, true, true, true, 400_000),
        ),
    ]);
    assert_eq!(ranked, vec![ids[1], ids[0]]);

    // 可见面积更大者在其余信号相同时胜出。
    let ranked = rank(&[
        (
            ids[0],
            RankingSignals::new(false, true, true, true, 100_000),
        ),
        (
            ids[1],
            RankingSignals::new(false, true, true, true, 400_000),
        ),
    ]);
    assert_eq!(ranked, vec![ids[1], ids[0]]);
}

#[test]
fn current_src_outranks_incidental_requests() {
    let ids = make_ids(&[
        "https://cdn.example.com/init.mp4",
        "https://cdn.example.com/playing.m3u8",
    ]);
    // 初始化分片（仅网络证据）不应排在当前播放媒体之前。
    let ranked = rank(&[
        (ids[0], RankingSignals::new(false, false, false, true, 0)),
        (ids[1], RankingSignals::new(true, true, true, true, 200_000)),
    ]);
    assert_eq!(ranked, vec![ids[1], ids[0]]);
}

#[test]
fn equal_signals_fall_back_to_stable_id_order() {
    let ids = make_ids(&[
        "https://cdn.example.com/a.m3u8",
        "https://cdn.example.com/b.m3u8",
        "https://cdn.example.com/c.m3u8",
    ]);
    let signals = RankingSignals::new(false, true, true, true, 50_000);
    // 打乱输入顺序，输出必须一致（相同时间/相同信号稳定排序）。
    let forward = rank(&[(ids[0], signals), (ids[1], signals), (ids[2], signals)]);
    let shuffled = rank(&[(ids[2], signals), (ids[0], signals), (ids[1], signals)]);
    assert_eq!(forward, shuffled);
    assert_eq!(forward, vec![ids[0], ids[1], ids[2]]);
    // 全零信号也稳定。
    let zero = RankingSignals::default();
    let ranked = rank(&[(ids[1], zero), (ids[0], zero)]);
    assert_eq!(ranked, vec![ids[0], ids[1]]);
}

#[test]
fn user_chosen_background_audio_still_ranks_first() {
    let ids = make_ids(&[
        "https://cdn.example.com/video.m3u8",
        "https://cdn.example.com/podcast.m3u8",
    ]);
    // 用户明确选择的后台音频：无可见面积但 current_src + audible。
    let ranked = rank(&[
        (
            ids[0],
            RankingSignals::new(false, false, false, true, 300_000),
        ),
        (ids[1], RankingSignals::new(true, true, true, true, 0)),
    ]);
    assert_eq!(ranked, vec![ids[1], ids[0]]);
}

#[test]
fn empty_input_ranks_empty() {
    assert!(rank(&[]).is_empty());
}
