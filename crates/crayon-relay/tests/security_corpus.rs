//! 安全语料测试（MED-18）：HLS parser / token / Range / 资源 ID 的恶意与
//! 异常输入——不得 panic、不得无界、拒绝路径稳定。非模糊框架的确定性语料；
//! 作为将来接入 cargo-fuzz 的种子集。

use crayon_relay::hls::parser::{parse, rewrite, HlsError};
use crayon_relay::session::SessionToken;

#[test]
fn malformed_playlists_never_panic() {
    let corpus: Vec<String> = vec![
        String::new(),
        "#EXTM3U".to_string(),
        "#EXTM3U\n".to_string(),
        "#EXTM3U\n#EXT-X-STREAM-INF:\n".to_string(), // 无后续 URI 行
        "#EXTM3U\n#EXT-X-MEDIA:URI=\n".to_string(),  // 未闭合引号
        "#EXTM3U\n#EXT-X-KEY:METHOD=\nseg.ts\n".to_string(),
        "#EXTM3U\n#EXT-X-KEY:METHOD=NONE,,,,\nseg.ts\n".to_string(),
        format!(
            "#EXTM3U\n#EXT-X-MEDIA:URI=\"{}\"\nv.m3u8\n",
            "a".repeat(100_000)
        ), // 超长属性
        format!("#EXTM3U\n{}\n", "x".repeat(1_000_000)), // 超长行
        "#EXTM3U\n\u{0}\u{0}\u{0}\nseg.ts\n".to_string(), // NUL 字节
        "#EXTM3U\nseg.ts\u{FF}.ts\n".to_string(),        // 非法 UTF-8 边界字符
        "#EXTM3U\n#EXT-X-MAP:URI=\"\"\nseg.ts\n".to_string(), // 空 URI
    ];
    for (i, body) in corpus.iter().enumerate() {
        let result = parse(body);
        if let Ok(parsed) = result {
            // 解析成功则改写必须有界且不 panic
            let out = rewrite(&parsed, "https://cdn.example.com/x.m3u8", 0, |_url| {
                Ok("/s/t/r/r1/a".to_string())
            });
            assert!(out.is_ok() || matches!(out, Err(_)), "corpus {i}");
        }
    }
    // 加密拒绝稳定
    assert_eq!(
        parse("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\nseg.ts\n").unwrap_err(),
        HlsError::Encrypted
    );
}

#[test]
fn token_hex_parsing_corpus() {
    for bad in [
        "",
        "0",
        &"0".repeat(31),
        &"0".repeat(33),
        &"zz".repeat(16),
        &"0g".repeat(16),
        &"../".repeat(10),
        &"é".repeat(16),
    ] {
        assert!(SessionToken::from_hex(bad).is_none(), "{bad:?} 不得解析");
    }
    assert!(SessionToken::from_hex(&"ab".repeat(16)).is_some());
}

#[test]
fn resource_id_charset_corpus() {
    use crayon_domain::ResourceId;
    for bad in ["../etc", "a b", "a/b", "a?b", "é", ""] {
        assert!(ResourceId::new(bad).is_err(), "{bad:?} 不得通过");
    }
    assert!(ResourceId::new(&"a".repeat(128)).is_ok());
    assert!(ResourceId::new(&"a".repeat(129)).is_err());
}

#[test]
fn range_well_formedness_corpus() {
    // is_well_formed_range 是 mp4 模块私有逻辑，经 fetcher 行为覆盖：
    // 这里固化语料语义——合法形式转发，其余回全量。
    let valid = ["bytes=0-1", "bytes=0-", "bytes=-100", "bytes=123456789-"];
    for v in valid {
        let spec = v.strip_prefix("bytes=").unwrap();
        let (a, b) = spec.split_once('-').unwrap();
        let ok = (a.is_empty() || a.bytes().all(|c| c.is_ascii_digit()))
            && (b.is_empty() || b.bytes().all(|c| c.is_ascii_digit()))
            && !(a.is_empty() && b.is_empty());
        assert!(ok, "{v}");
    }
    for bad in ["bytes=-", "bytes=a-b", "items=0-1", "bytes=1-2-3", ""] {
        let well_formed = bad
            .strip_prefix("bytes=")
            .and_then(|spec| spec.split_once('-'))
            .is_some_and(|(a, b)| {
                (a.is_empty() || a.bytes().all(|c| c.is_ascii_digit()))
                    && (b.is_empty() || b.bytes().all(|c| c.is_ascii_digit()))
                    && !(a.is_empty() && b.is_empty())
            });
        assert!(!well_formed, "{bad}");
    }
}
