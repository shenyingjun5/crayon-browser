use super::*;
use serde_json::{json, Value};

fn navigation() -> NavigationBinding {
    NavigationBinding::new(
        TabId::new("tab-content-1").expect("valid tab id"),
        SessionGeneration::from_raw(4),
    )
}

fn all_blocks() -> Vec<ContentBlock> {
    vec![
        ContentBlock::Heading {
            level: 1,
            text: "Title".to_owned(),
        },
        ContentBlock::Paragraph {
            text: "Intro".to_owned(),
        },
        ContentBlock::ListItem {
            depth: 1,
            ordinal: Some(1),
            text: "First".to_owned(),
        },
        ContentBlock::Link {
            href: "https://example.com/detail".to_owned(),
            text: "Details".to_owned(),
        },
        ContentBlock::Image {
            src: "https://example.com/image.png".to_owned(),
            alt: "Diagram".to_owned(),
        },
        ContentBlock::Table {
            rows: vec![
                TableRow {
                    cells: vec!["A".to_owned(), "B".to_owned()],
                },
                TableRow {
                    cells: vec!["1".to_owned(), "2".to_owned()],
                },
            ],
        },
        ContentBlock::CodeBlock {
            language: Some("rust".to_owned()),
            text: "fn main() {}".to_owned(),
        },
        ContentBlock::Divider,
        ContentBlock::Quote {
            text: "Quoted".to_owned(),
        },
    ]
}

fn sample_snapshot() -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        navigation(),
        "https://example.com/article".to_owned(),
        "Example article".to_owned(),
        7,
        TruncationInfo {
            truncated: true,
            omitted_blocks: 2,
            omitted_bytes: 64,
            reasons: vec![
                TruncationReason::LimitBlockCount,
                TruncationReason::LimitTotalBytes,
            ],
        },
        all_blocks(),
    )
    .expect("sample snapshot is valid")
}

fn replace_url(block: ContentBlock, url: &str) -> ContentBlock {
    match block {
        ContentBlock::Link { text, .. } => ContentBlock::Link {
            href: url.to_owned(),
            text,
        },
        ContentBlock::Image { alt, .. } => ContentBlock::Image {
            src: url.to_owned(),
            alt,
        },
        other => other,
    }
}

#[test]
fn ct_001_current_and_previous_golden_are_byte_stable() {
    let encoded = serde_json::to_string(&sample_snapshot()).expect("serialize snapshot");
    let current = include_str!("../../../schemas/current/page_snapshot_v1.json").trim_end();
    let previous = include_str!("../../../schemas/previous/page_snapshot_v1.json").trim_end();

    assert_eq!(encoded, current);
    assert_eq!(current, previous);
    let decoded: PageSnapshot = serde_json::from_str(current).expect("decode current golden");
    assert_eq!(decoded, sample_snapshot());
    assert_eq!(serde_json::to_string(&decoded).unwrap(), current);
}

#[test]
fn ct_001_nine_closed_block_kinds_round_trip() {
    let snapshot = sample_snapshot();
    assert_eq!(snapshot.blocks().len(), 9);
    let encoded = serde_json::to_vec(&snapshot).unwrap();
    let decoded: PageSnapshot = serde_json::from_slice(&encoded).unwrap();
    assert_eq!(decoded.blocks(), snapshot.blocks());
    assert_eq!(decoded.schema_version(), SchemaVersion::CURRENT);
    assert_eq!(decoded.output_level(), OutputLevel::Standard);
    assert_eq!(decoded.navigation(), &navigation());
    assert_eq!(decoded.url(), "https://example.com/article");
    assert_eq!(decoded.title(), "Example article");
    assert_eq!(decoded.revision(), 7);
    assert_eq!(
        decoded.provenance().verified_by(),
        VERIFIED_BY_BROWSER_PROCESS
    );
    assert!(decoded.truncation().truncated);
}

#[test]
fn ct_001_compact_and_standard_limits_are_distinct_and_enforced() {
    let blocks = vec![
        ContentBlock::Paragraph {
            text: "x".to_owned(),
        };
        OutputLevel::Compact.max_blocks() + 1
    ];
    let compact = PageSnapshot::new(
        OutputLevel::Compact,
        navigation(),
        "https://example.com/".to_owned(),
        "Title".to_owned(),
        0,
        TruncationInfo::default(),
        blocks.clone(),
    );
    assert_eq!(compact, Err(SnapshotError::BlockCountExceeded));
    assert!(PageSnapshot::new(
        OutputLevel::Standard,
        navigation(),
        "https://example.com/".to_owned(),
        "Title".to_owned(),
        0,
        TruncationInfo::default(),
        blocks,
    )
    .is_ok());

    let oversized = ContentBlock::Paragraph {
        text: "x".repeat(OutputLevel::Compact.max_block_text_bytes() + 1),
    };
    assert_eq!(
        PageSnapshot::new(
            OutputLevel::Compact,
            navigation(),
            "https://example.com/".to_owned(),
            "Title".to_owned(),
            0,
            TruncationInfo::default(),
            vec![oversized.clone()],
        ),
        Err(SnapshotError::BlockOutOfBounds)
    );
    assert!(PageSnapshot::new(
        OutputLevel::Standard,
        navigation(),
        "https://example.com/".to_owned(),
        "Title".to_owned(),
        0,
        TruncationInfo::default(),
        vec![oversized],
    )
    .is_ok());

    let total_overflow = vec![
        ContentBlock::Paragraph {
            text: "x".repeat(OutputLevel::Compact.max_block_text_bytes()),
        };
        65
    ];
    assert_eq!(
        PageSnapshot::new(
            OutputLevel::Compact,
            navigation(),
            "https://example.com/".to_owned(),
            "Title".to_owned(),
            0,
            TruncationInfo::default(),
            total_overflow,
        ),
        Err(SnapshotError::TotalBytesExceeded)
    );
}

#[test]
fn ct_002_decoding_revalidates_provenance_unknown_fields_and_limits() {
    let mut value = serde_json::to_value(sample_snapshot()).unwrap();
    value["provenance"]["verified_by"] = json!("renderer");
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());

    let mut value = serde_json::to_value(sample_snapshot()).unwrap();
    value["unknown"] = json!(true);
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());

    let mut value = serde_json::to_value(sample_snapshot()).unwrap();
    value["blocks"][0]["unknown"] = json!(true);
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());

    let mut value = serde_json::to_value(sample_snapshot()).unwrap();
    value["title"] = json!("x".repeat(limits::MAX_TITLE_BYTES + 1));
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());

    let mut value = serde_json::to_value(sample_snapshot()).unwrap();
    value["schema_version"] = json!(2);
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());
}

#[test]
fn ct_003_dangerous_and_ambiguous_urls_are_rejected() {
    for url in [
        "javascript:alert(1)",
        "data:text/plain,payload",
        "blob:https://example.com/id",
        "file:///tmp/secret",
        "https://",
        "https://user@example.com/private",
        "https://example.com/has space",
        "https://example.com\\evil",
        "https://example.com/\nheader",
        "https://example.com/\u{0085}control",
    ] {
        assert!(!is_safe_url(url), "unexpected safe URL: {url}");
        let mut snapshot = sample_snapshot();
        snapshot.url = url.to_owned();
        assert_eq!(snapshot.validate(), Err(SnapshotError::InvalidUrl));

        for index in [3_usize, 4] {
            let mut snapshot = sample_snapshot();
            snapshot.blocks[index] = replace_url(snapshot.blocks[index].clone(), url);
            assert_eq!(snapshot.validate(), Err(SnapshotError::InvalidUrl));
        }
    }
    for url in [
        "http://example.com/",
        "HTTPS://example.com/path?q=1#part",
        "https://例子.test/路径",
    ] {
        assert!(is_safe_url(url), "unexpected rejected URL: {url}");
    }
}

#[test]
fn ct_002_shapes_and_truncation_are_fail_closed() {
    let mut snapshot = sample_snapshot();
    snapshot.blocks[0] = ContentBlock::Heading {
        level: 7,
        text: "bad".to_owned(),
    };
    assert_eq!(snapshot.validate(), Err(SnapshotError::ShapeInvalid));

    let mut snapshot = sample_snapshot();
    snapshot.blocks[6] = ContentBlock::CodeBlock {
        language: Some("rust".to_owned()),
        text: "let value = 1;\u{0085}".to_owned(),
    };
    assert_eq!(snapshot.validate(), Err(SnapshotError::BlockOutOfBounds));

    let mut snapshot = sample_snapshot();
    snapshot.truncation = TruncationInfo {
        truncated: true,
        omitted_blocks: 0,
        omitted_bytes: 0,
        reasons: vec![TruncationReason::LimitBlockCount],
    };
    assert_eq!(
        snapshot.validate(),
        Err(SnapshotError::TruncatedButNothingOmitted)
    );

    let mut snapshot = sample_snapshot();
    snapshot.truncation = TruncationInfo {
        truncated: false,
        omitted_blocks: 1,
        omitted_bytes: 0,
        reasons: vec![TruncationReason::LimitBlockCount],
    };
    assert_eq!(
        snapshot.validate(),
        Err(SnapshotError::TruncationInconsistent)
    );

    let mut snapshot = sample_snapshot();
    snapshot.truncation.reasons = vec![TruncationReason::LimitDepth, TruncationReason::LimitDepth];
    assert_eq!(
        snapshot.validate(),
        Err(SnapshotError::DuplicateTruncationReason)
    );

    let mut value = serde_json::to_value(sample_snapshot()).unwrap();
    value["truncation"]["reasons"] = json!(["unbounded_unknown_reason"]);
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());
}

#[test]
fn ct_002_deterministic_malformed_input_probe_never_panics() {
    let mut state = 0x5eed_cafe_u64;
    for length in 0..512_usize {
        let mut bytes = Vec::with_capacity(length);
        for _ in 0..length {
            state = state
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1);
            bytes.push((state >> 32) as u8);
        }
        let result = std::panic::catch_unwind(|| serde_json::from_slice::<PageSnapshot>(&bytes));
        assert!(result.is_ok());
    }

    let malformed: Value = json!({
        "schema_version": 1,
        "output_level": "standard",
        "navigation": {"tab_id": "bad tab", "generation": 0},
        "url": "https://example.com/",
        "title": "title",
        "revision": 0,
        "provenance": {"verified_by": "browser_process"},
        "truncation": {"truncated": false, "omitted_blocks": 0,
            "omitted_bytes": 0, "reasons": []},
        "blocks": []
    });
    assert!(serde_json::from_value::<PageSnapshot>(malformed).is_err());
}
