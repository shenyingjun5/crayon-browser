use super::*;
use crate::{NavigationBinding, OutputLevel, TableRow, TruncationInfo};
use crayon_domain::{SessionGeneration, TabId};

fn snapshot() -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(
            TabId::new("index-tab").unwrap(),
            SessionGeneration::from_raw(1),
        ),
        "https://example.test/index".to_owned(),
        "Index".to_owned(),
        3,
        TruncationInfo::default(),
        vec![
            ContentBlock::Heading {
                level: 1,
                text: "h".into(),
            },
            ContentBlock::Paragraph { text: "p".into() },
            ContentBlock::ListItem {
                depth: 1,
                ordinal: None,
                text: "l".into(),
            },
            ContentBlock::Link {
                href: "https://example.test/a".into(),
                text: "a".into(),
            },
            ContentBlock::Quote { text: "q".into() },
            ContentBlock::CodeBlock {
                language: None,
                text: "c".into(),
            },
            ContentBlock::Image {
                src: "https://example.test/i".into(),
                alt: "i".into(),
            },
            ContentBlock::Table {
                rows: vec![TableRow {
                    cells: vec!["t".into()],
                }],
            },
            ContentBlock::Divider,
            ContentBlock::Paragraph { text: "p2".into() },
        ],
    )
    .unwrap()
}

#[test]
fn ct_006_nine_field_indexes_are_ordered_and_bounded() {
    let index = SnapshotIndex::build(&snapshot());
    assert_eq!(index.revision(), 3);
    assert_eq!(index.total_positions(), 10);
    assert_eq!(index.positions(BlockKind::Heading), &[0]);
    assert_eq!(index.positions(BlockKind::Paragraph), &[1, 9]);
    assert_eq!(index.positions(BlockKind::ListItem), &[2]);
    assert_eq!(index.positions(BlockKind::Link), &[3]);
    assert_eq!(index.positions(BlockKind::Quote), &[4]);
    assert_eq!(index.positions(BlockKind::CodeBlock), &[5]);
    assert_eq!(index.positions(BlockKind::Image), &[6]);
    assert_eq!(index.positions(BlockKind::Table), &[7]);
    assert_eq!(index.positions(BlockKind::Divider), &[8]);
    assert!(index.payload_bytes() >= 10);
}

#[test]
fn empty_snapshot_has_no_positions() {
    let empty = PageSnapshot::new(
        OutputLevel::Compact,
        NavigationBinding::new(
            TabId::new("empty-index").unwrap(),
            SessionGeneration::from_raw(1),
        ),
        "https://example.test/empty".into(),
        "Empty".into(),
        1,
        TruncationInfo::default(),
        Vec::new(),
    )
    .unwrap();
    let index = SnapshotIndex::build(&empty);
    assert_eq!(index.total_positions(), 0);
    assert!(index.positions(BlockKind::Paragraph).is_empty());
}
