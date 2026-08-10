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
