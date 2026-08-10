//! CandidateStore contract (MED-02): multi-source merge with evidence
//! preservation (PL-001), query-preserving storage with redacted logging
//! identity (PL-002), and bounded capacity.

use crayon_domain::TabId;
use crayon_media_observer::candidate::{CandidateStore, MAX_CANDIDATES, MAX_EVIDENCE};
use crayon_media_observer::{FrameContext, NavigationId, ObservationSource, SourceObservation};

fn obs(url: &str, source: ObservationSource) -> SourceObservation {
    SourceObservation::new(
        TabId::new("tab-01").unwrap(),
        NavigationId::INITIAL,
        FrameContext::Main,
        source,
        url.to_string(),
        "https://example.com/watch".to_string(),
        100,
    )
    .unwrap()
}

#[test]
fn pl_001_same_url_from_multiple_sources_merges_with_evidence() {
    let mut store = CandidateStore::new();
    let url = "https://cdn.example.com/v.mp4";
    let first = store.ingest(&obs(url, ObservationSource::DomMediaElement));
    let second = store.ingest(&obs(url, ObservationSource::NetworkRequest));
    let third = store.ingest(&obs(url, ObservationSource::CurrentSrc));

    assert_eq!(first, second);
    assert_eq!(second, third);
    assert_eq!(store.len(), 1);

    let entry = store.get(first.unwrap()).unwrap();
    assert_eq!(entry.evidence().len(), 3);
    let sources: Vec<_> = entry.evidence().iter().map(|e| e.source).collect();
    assert!(sources.contains(&ObservationSource::DomMediaElement));
    assert!(sources.contains(&ObservationSource::NetworkRequest));
    assert!(sources.contains(&ObservationSource::CurrentSrc));

    // Duplicate evidence of the same source+frame is not recorded twice.
    store.ingest(&obs(url, ObservationSource::NetworkRequest));
    assert_eq!(store.get(first.unwrap()).unwrap().evidence().len(), 3);
}

#[test]
fn different_urls_and_navigations_do_not_merge() {
    let mut store = CandidateStore::new();
    let a = store.ingest(&obs(
        "https://cdn.example.com/a.mp4",
        ObservationSource::NetworkRequest,
    ));
    let b = store.ingest(&obs(
        "https://cdn.example.com/b.mp4",
        ObservationSource::NetworkRequest,
    ));
    assert_ne!(a, b);
    assert_eq!(store.len(), 2);

    // 同 URL 但属于新的 navigation：新候选（旧候选由生命周期任务清理）。
    let mut after_nav = obs(
        "https://cdn.example.com/a.mp4",
        ObservationSource::NetworkRequest,
    );
    after_nav = SourceObservation::new(
        after_nav.tab_id().clone(),
        NavigationId::new(1),
        after_nav.frame(),
        after_nav.source(),
        after_nav.url().to_string(),
        after_nav.page_url().to_string(),
        200,
    )
    .unwrap();
    let c = store.ingest(&after_nav);
    assert_ne!(a, c);
    assert_eq!(store.len(), 3);
}

#[test]
fn pl_002_signature_query_preserved_but_never_logged() {
    let mut store = CandidateStore::new();
    let url = "https://cdn.example.com/live/master.m3u8?sign=abc123&token=xyz&t=1754";
    let id = store
        .ingest(&obs(url, ObservationSource::NetworkRequest))
        .unwrap();

    // 完整 URL（含短期签名 query）原样保存在可信内存。
    let entry = store.get(id).unwrap();
    assert_eq!(entry.url(), url);

    // 脱敏视图与 Debug 输出只有 origin + 不透明 ID，不含 query/签名。
    let redacted = store.redacted(id).unwrap();
    assert_eq!(redacted.origin, "https://cdn.example.com");
    let debug = format!("{entry:?}");
    for leak in ["sign=abc123", "token=xyz", "master.m3u8"] {
        assert!(
            !debug.contains(leak),
            "debug must not contain {leak}: {debug}"
        );
    }
    let redacted_debug = format!("{redacted:?}");
    assert!(!redacted_debug.contains("sign="));
}

#[test]
fn merge_normalizes_case_and_default_port_only() {
    let mut store = CandidateStore::new();
    let a = store.ingest(&obs(
        "https://CDN.EXAMPLE.COM/v.mp4?x=1",
        ObservationSource::NetworkRequest,
    ));
    let b = store.ingest(&obs(
        "https://cdn.example.com:443/v.mp4?x=1",
        ObservationSource::CurrentSrc,
    ));
    assert_eq!(a, b, "大小写与默认端口差异应合并");
    // query 不同的 URL 不合并（签名参数不得被错误去重）。
    let c = store.ingest(&obs(
        "https://cdn.example.com/v.mp4?x=2",
        ObservationSource::NetworkRequest,
    ));
    assert_ne!(a, c);
}

#[test]
fn store_capacity_is_bounded() {
    let mut store = CandidateStore::new();
    for i in 0..MAX_CANDIDATES {
        let url = format!("https://cdn.example.com/v{i}.mp4");
        assert!(store
            .ingest(&obs(&url, ObservationSource::NetworkRequest))
            .is_some());
    }
    let overflow = store.ingest(&obs(
        "https://cdn.example.com/overflow.mp4",
        ObservationSource::NetworkRequest,
    ));
    assert_eq!(overflow, None, "满载必须拒绝而非无界增长");
    assert_eq!(store.len(), MAX_CANDIDATES);

    // evidence 同样有界
    let mut store = CandidateStore::new();
    let url = "https://cdn.example.com/v.mp4";
    let id = store
        .ingest(&obs(url, ObservationSource::NetworkRequest))
        .unwrap();
    let entry = store.get(id).unwrap();
    assert!(entry.evidence().len() <= MAX_EVIDENCE);
}
