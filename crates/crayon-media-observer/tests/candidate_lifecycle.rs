//! Candidate lifecycle contract (MED-04): navigation invalidation (BR-007),
//! tab-close tombstones (BR-013), TTL expiry (PL-012) and bounded capacity
//! with eviction.

use crayon_domain::TabId;
use crayon_media_observer::candidate::store::MAX_TABS;
use crayon_media_observer::candidate::{CandidateStore, LifecyclePolicy, MAX_CANDIDATES};
use crayon_media_observer::{FrameContext, NavigationId, ObservationSource, SourceObservation};

fn obs_at(tab: &str, nav: u64, url: &str, at_ms: u64) -> SourceObservation {
    SourceObservation::new(
        TabId::new(tab).unwrap(),
        NavigationId::new(nav),
        FrameContext::Main,
        ObservationSource::NetworkRequest,
        url.to_string(),
        "https://example.com/watch".to_string(),
        at_ms,
    )
    .unwrap()
}

#[test]
fn br_007_navigation_drops_old_candidates_and_rejects_late_events() {
    let mut store = CandidateStore::new();
    let old = store.ingest(&obs_at("tab-01", 0, "https://cdn.example.com/a.mp4", 100));
    assert!(old.is_some());
    store.ingest(&obs_at("tab-01", 0, "https://cdn.example.com/b.mp4", 100));

    // 导航：旧候选全部失效，幂等。
    assert_eq!(
        store.on_navigation(&TabId::new("tab-01").unwrap(), NavigationId::new(1)),
        2
    );
    assert_eq!(
        store.on_navigation(&TabId::new("tab-01").unwrap(), NavigationId::new(1)),
        0
    );
    assert_eq!(store.len(), 0);
    assert!(store.get(old.unwrap()).is_none());

    // 旧 frame/worker 的迟到上报被拒绝，不能重建候选。
    assert_eq!(
        store.ingest(&obs_at(
            "tab-01",
            0,
            "https://cdn.example.com/late.mp4",
            150
        )),
        None
    );
    assert_eq!(store.len(), 0);

    // 新 navigation 的观察正常工作。
    assert!(store
        .ingest(&obs_at("tab-01", 1, "https://cdn.example.com/new.mp4", 200))
        .is_some());
}

#[test]
fn br_013_tab_close_tombstones_late_events() {
    let mut store = CandidateStore::new();
    store.ingest(&obs_at("tab-01", 0, "https://cdn.example.com/a.mp4", 100));

    assert_eq!(store.on_tab_close(&TabId::new("tab-01").unwrap()), 1);
    assert_eq!(store.on_tab_close(&TabId::new("tab-01").unwrap()), 0); // 幂等
                                                                       // 关闭后迟到的探测事件（仍在进行的 observer）不能重建候选。
    assert_eq!(
        store.ingest(&obs_at(
            "tab-01",
            0,
            "https://cdn.example.com/late.mp4",
            150
        )),
        None
    );
    assert_eq!(store.len(), 0);
    // 同标签以新 navigation 重开（浏览器复用标签对象）则恢复接收。
    assert!(store
        .ingest(&obs_at(
            "tab-01",
            1,
            "https://cdn.example.com/reopened.mp4",
            300
        ))
        .is_some());
}

#[test]
fn pl_012_ttl_expiry_forces_replanning() {
    let mut store = CandidateStore::new();
    let id = store
        .ingest(&obs_at("tab-01", 0, "https://cdn.example.com/a.mp4", 100))
        .unwrap();
    let policy = LifecyclePolicy::DEFAULT;
    // 边界：恰好 TTL 时刻未过期，TTL+1 过期。
    assert_eq!(store.expire_stale(100 + policy.ttl_ms(), policy), 0);
    assert_eq!(store.expire_stale(100 + policy.ttl_ms() + 1, policy), 1);
    assert!(store.get(id).is_none(), "过期候选不得再被规划复用");
    assert_eq!(store.expire_stale(100 + policy.ttl_ms() + 1, policy), 0); // 幂等
}

#[test]
fn full_store_evicts_expired_first_then_stalest() {
    // 满载且部分过期：先驱逐过期候选。
    let mut store = CandidateStore::new();
    let mut early_ids = Vec::new();
    for i in 0..MAX_CANDIDATES {
        let at = if i < 10 { 100 } else { 1_000_000 };
        let url = format!("https://cdn.example.com/v{i}.mp4");
        let id = store.ingest(&obs_at("tab-01", 0, &url, at)).unwrap();
        if i < 10 {
            early_ids.push(id);
        }
    }
    let now = 100 + LifecyclePolicy::DEFAULT.ttl_ms() + 1;
    let overflow = store.ingest(&obs_at("tab-01", 0, "https://cdn.example.com/new.mp4", now));
    assert!(overflow.is_some(), "驱逐过期候选后接收新候选");
    for id in early_ids {
        assert!(store.get(id).is_none(), "过期候选应被驱逐");
    }
    assert_eq!(store.len(), 247);

    // 全部新鲜：驱逐 last_observed 最小者。
    let mut store = CandidateStore::new();
    let base = 1_000_000u64;
    let mut ids = Vec::new();
    for i in 0..MAX_CANDIDATES {
        let url = format!("https://cdn.example.com/v{i}.mp4");
        ids.push(
            store
                .ingest(&obs_at("tab-01", 0, &url, base + i as u64))
                .unwrap(),
        );
    }
    let newest = store.ingest(&obs_at(
        "tab-01",
        0,
        "https://cdn.example.com/new.mp4",
        base + 1000,
    ));
    assert!(newest.is_some());
    assert!(store.get(ids[0]).is_none(), "最旧候选应被驱逐");
    assert!(store.get(ids[1]).is_some(), "次旧候选保留");
    assert_eq!(store.len(), MAX_CANDIDATES);
}

#[test]
fn tab_table_is_bounded() {
    let mut store = CandidateStore::new();
    for i in 0..MAX_TABS {
        let tab = format!("tab-{i:03}");
        assert!(store
            .ingest(&obs_at(&tab, 0, "https://cdn.example.com/v.mp4", 100))
            .is_some());
    }
    assert_eq!(
        store.ingest(&obs_at(
            "tab-overflow",
            0,
            "https://cdn.example.com/v.mp4",
            100
        )),
        None,
        "标签表满载拒绝而非无界增长"
    );
}
