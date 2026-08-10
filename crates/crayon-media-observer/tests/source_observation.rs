//! SourceObservation contract (MED-01): URL/scheme/length validation,
//! frame/navigation/source facts, and the stale-navigation discard rule.

use crayon_domain::TabId;
use crayon_media_observer::{
    FrameContext, NavigationId, ObservationError, ObservationSource, SourceObservation,
};

fn observation(url: &str) -> Result<SourceObservation, ObservationError> {
    SourceObservation::new(
        TabId::new("tab-01").unwrap(),
        NavigationId::INITIAL,
        FrameContext::Main,
        ObservationSource::NetworkRequest,
        url.to_string(),
        "https://example.com/watch".to_string(),
        1000,
    )
}

#[test]
fn valid_observation_carries_all_facts() {
    let obs = observation("https://cdn.example.com/master.m3u8?sign=abc").unwrap();
    assert_eq!(obs.url(), "https://cdn.example.com/master.m3u8?sign=abc");
    assert_eq!(obs.page_url(), "https://example.com/watch");
    assert_eq!(obs.tab_id().as_str(), "tab-01");
    assert_eq!(obs.navigation(), NavigationId::INITIAL);
    assert_eq!(obs.frame(), FrameContext::Main);
    assert_eq!(obs.source(), ObservationSource::NetworkRequest);
    assert_eq!(obs.observed_at_ms(), 1000);
}

#[test]
fn rejects_empty_overlong_and_non_http_urls() {
    assert_eq!(observation(""), Err(ObservationError::EmptyUrl));
    assert_eq!(
        observation(&format!("https://example.com/{}", "a".repeat(2048))),
        Err(ObservationError::UrlTooLong)
    );
    assert_eq!(
        observation("ftp://example.com/v.mp4"),
        Err(ObservationError::UnsupportedScheme)
    );
    assert_eq!(
        observation("file:///etc/passwd"),
        Err(ObservationError::UnsupportedScheme)
    );
    assert_eq!(
        observation("//example.com/v.mp4"),
        Err(ObservationError::UnsupportedScheme)
    );
    // Boundary: exactly 2048 bytes is accepted.
    let url = format!("https://example.com/{}", "a".repeat(2048 - 20));
    assert_eq!(url.len(), 2048);
    assert!(observation(&url).is_ok());
}

#[test]
fn br_007_stale_navigation_events_are_detectable() {
    let obs = observation("https://cdn.example.com/v.mp4").unwrap();
    assert!(obs.is_current(NavigationId::INITIAL));
    let after_navigation = NavigationId::new(1);
    assert!(
        !obs.is_current(after_navigation),
        "导航后的迟到上报必须可判定丢弃"
    );
}

#[test]
fn br_008_iframe_worker_mse_observations_keep_provenance() {
    let tab = TabId::new("tab-01").unwrap();
    for (frame, source) in [
        (FrameContext::Subframe, ObservationSource::DomMediaElement),
        (FrameContext::Main, ObservationSource::WorkerFetch),
        (FrameContext::Main, ObservationSource::MseSourceBuffer),
    ] {
        let obs = SourceObservation::new(
            tab.clone(),
            NavigationId::INITIAL,
            frame,
            source,
            "https://cdn.example.com/seg0.ts".to_string(),
            "https://example.com/watch".to_string(),
            42,
        )
        .unwrap();
        assert_eq!(obs.frame(), frame);
        assert_eq!(obs.source(), source);
    }
    // 类型层面不含正文/表单/Cookie 字段：可序列化事实只有上述成员。
    let obs = observation("https://cdn.example.com/v.mp4").unwrap();
    let debug = format!("{obs:?}");
    for forbidden in ["cookie", "authorization", "body", "password"] {
        assert!(
            !debug.to_ascii_lowercase().contains(forbidden),
            "observation must not carry {forbidden}"
        );
    }
}
