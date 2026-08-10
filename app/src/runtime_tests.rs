//! 去重写入测试：`push_hit` 的空 URL 忽略、同 URL 保留首次、追加顺序。

use super::*;

#[test]
fn push_hit_ignores_empty_url() {
    let hits = Mutex::new(Vec::new());
    push_hit(
        &hits,
        String::new(),
        "https://example.com/".to_string(),
        None,
    );
    assert!(hits.lock().unwrap().is_empty());
}

#[test]
fn push_hit_dedup_keeps_first_occurrence() {
    let hits = Mutex::new(Vec::new());
    push_hit(
        &hits,
        "https://cdn.example.com/a.m3u8".to_string(),
        "page-1".to_string(),
        Some("hls".to_string()),
    );
    // 同 URL 再次上报（IPC + beacon 双通道常态）：不追加，保留首次的 page/proto
    push_hit(
        &hits,
        "https://cdn.example.com/a.m3u8".to_string(),
        "page-2".to_string(),
        None,
    );
    push_hit(
        &hits,
        "https://cdn.example.com/b.mp4".to_string(),
        "page-1".to_string(),
        None,
    );
    let g = hits.lock().unwrap();
    assert_eq!(g.len(), 2);
    assert_eq!(g[0].url, "https://cdn.example.com/a.m3u8");
    assert_eq!(g[0].page, "page-1");
    assert_eq!(g[0].proto.as_deref(), Some("hls"));
    assert_eq!(g[1].url, "https://cdn.example.com/b.mp4");
}
