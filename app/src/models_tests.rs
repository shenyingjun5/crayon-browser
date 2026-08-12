//! 模型序列化 golden 测试：锁定 `SniffResponse`/`SniffResultItem` 的 JSON
//! 字段名、字段顺序与可选字段省略行为（前端与 CLI marker 依赖该字节格式）。

use super::*;

#[test]
fn sniff_response_full_golden() {
    let resp = SniffResponse {
        page: "https://example.com/page".to_string(),
        count: 1,
        results: vec![SniffResultItem {
            index: 0,
            url: "https://cdn.example.com/v/index.m3u8".to_string(),
            protocol: "hls".to_string(),
            quality: Some("1080p".to_string()),
            drm: false,
            restriction: Some("WASM 私有加扰，实测解码画面异常，无法播放".to_string()),
            relay_url: Some("http://127.0.0.1:8321/proxy/abc".to_string()),
            codec: Some("H.264+AAC · TS".to_string()),
        }],
        note: Some("示例备注".to_string()),
    };
    let json = serde_json::to_string(&resp).unwrap();
    assert_eq!(
        json,
        r#"{"page":"https://example.com/page","count":1,"results":[{"index":0,"url":"https://cdn.example.com/v/index.m3u8","protocol":"hls","quality":"1080p","drm":false,"restriction":"WASM 私有加扰，实测解码画面异常，无法播放","relay_url":"http://127.0.0.1:8321/proxy/abc","codec":"H.264+AAC · TS"}],"note":"示例备注"}"#
    );
}

#[test]
fn sniff_response_optional_fields_omitted() {
    let resp = SniffResponse {
        page: "https://example.com/".to_string(),
        count: 0,
        results: vec![],
        note: None,
    };
    assert_eq!(
        serde_json::to_string(&resp).unwrap(),
        r#"{"page":"https://example.com/","count":0,"results":[]}"#
    );

    let item = SniffResultItem {
        index: 2,
        url: "https://cdn.example.com/v.mp4".to_string(),
        protocol: "mp4".to_string(),
        quality: None,
        drm: true,
        restriction: None,
        relay_url: None,
        codec: None,
    };
    assert_eq!(
        serde_json::to_string(&item).unwrap(),
        r#"{"index":2,"url":"https://cdn.example.com/v.mp4","protocol":"mp4","drm":true}"#
    );
}

/// 构造一个可自定义关键字段的嗅探结果项。
fn sniff_item(
    protocol: &str,
    drm: bool,
    restriction: Option<&str>,
    relay_url: Option<&str>,
    codec: Option<&str>,
) -> SniffResultItem {
    SniffResultItem {
        index: 0,
        url: format!("https://cdn.example.com/v.{protocol}"),
        protocol: protocol.to_string(),
        quality: None,
        drm,
        restriction: restriction.map(str::to_string),
        relay_url: relay_url.map(str::to_string),
        codec: codec.map(str::to_string),
    }
}

#[test]
fn sniff_probe_targets_filters_candidates() {
    let resp = SniffResponse {
        page: "https://example.com/".to_string(),
        count: 6,
        results: vec![
            // 可探：hls + relay + webview 可解码编码
            sniff_item(
                "hls",
                false,
                None,
                Some("http://127.0.0.1:8321/proxy/a"),
                Some("H.264+AAC · TS"),
            ),
            // 已受限：跳过
            sniff_item("hls", false, Some("WASM 私有加扰"), None, None),
            // DRM：跳过
            sniff_item("hls", true, None, None, None),
            // 非 hls/mp4 协议：跳过
            sniff_item(
                "dash",
                false,
                None,
                Some("http://127.0.0.1:8321/dashmpd/x"),
                None,
            ),
            // webview 解不了的编码（HEVC）：跳过，避免「没画面 ≠ 流坏」误判
            sniff_item(
                "hls",
                false,
                None,
                Some("http://127.0.0.1:8321/proxy/b"),
                Some("HEVC+AAC · TS"),
            ),
            // 无 relay_url：跳过
            sniff_item("mp4", false, None, None, None),
        ],
        note: None,
    };
    let targets = sniff_probe_targets(&resp);
    assert_eq!(targets.len(), 1);
    assert_eq!(targets[0].url, "https://cdn.example.com/v.hls");
    assert_eq!(targets[0].relay_url, "http://127.0.0.1:8321/proxy/a");
}

#[test]
fn extract_probe_targets_same_criteria() {
    let fmt = |protocol: &str,
               drm: bool,
               restriction: Option<&str>,
               relay_url: Option<&str>,
               codec: Option<&str>| {
        crayon_browser_core::extract::Format {
            url: format!("https://cdn.example.com/v.{protocol}"),
            protocol: protocol.to_string(),
            quality: None,
            drm,
            restriction: restriction.map(str::to_string),
            headers: std::collections::HashMap::new(),
            relay_url: relay_url.map(str::to_string),
            codec: codec.map(str::to_string),
        }
    };
    let info = crayon_browser_core::extract::VideoInfo {
        title: None,
        webpage: "https://example.com/".to_string(),
        source: "static".to_string(),
        formats: vec![
            fmt(
                "mp4",
                false,
                None,
                Some("http://127.0.0.1:8321/proxy/m"),
                None,
            ),
            fmt("mp4", false, Some("流地址失效"), None, None),
            fmt("hls", true, None, None, None),
            fmt(
                "hls",
                false,
                None,
                Some("http://127.0.0.1:8321/proxy/h"),
                Some("AV1+AAC · fMP4"),
            ),
        ],
        note: None,
    };
    let targets = extract_probe_targets(&info);
    assert_eq!(targets.len(), 1);
    assert_eq!(targets[0].url, "https://cdn.example.com/v.mp4");
    assert_eq!(targets[0].relay_url, "http://127.0.0.1:8321/proxy/m");
}
