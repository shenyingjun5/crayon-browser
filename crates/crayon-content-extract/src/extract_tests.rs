use super::*;

fn key(section: u16, column: u16, row: u32, source_order: u32) -> ReadingKey {
    ReadingKey {
        section,
        column,
        row,
        source_order,
    }
}

fn paragraph(node_id: u64, region_id: u32, kind: RegionKind, text: &str) -> SourceFact {
    SourceFact {
        node_id,
        region_id,
        region_kind: kind,
        reading_key: key(0, 0, node_id as u32, node_id as u32),
        visible: true,
        same_origin: true,
        privacy: PrivacyClass::Public,
        content: SourceContent::Paragraph {
            text: text.to_owned(),
        },
    }
}

fn block_text(block: &ContentBlock) -> &str {
    match block {
        ContentBlock::Heading { text, .. }
        | ContentBlock::Paragraph { text }
        | ContentBlock::ListItem { text, .. }
        | ContentBlock::Link { text, .. }
        | ContentBlock::Quote { text }
        | ContentBlock::CodeBlock { text, .. } => text,
        ContentBlock::Image { alt, .. } => alt,
        ContentBlock::Table { .. } | ContentBlock::Divider => "",
    }
}

#[test]
fn ct_004_selects_main_region_and_stable_multicolumn_order() {
    let mut facts = vec![
        paragraph(1, 10, RegionKind::Navigation, "home topics account"),
        paragraph(2, 20, RegionKind::Article, "right top"),
        paragraph(3, 20, RegionKind::Article, "left bottom"),
        paragraph(4, 20, RegionKind::Article, "left top"),
    ];
    facts[1].reading_key = key(0, 1, 0, 1);
    facts[2].reading_key = key(0, 0, 1, 2);
    facts[3].reading_key = key(0, 0, 0, 3);
    let result = extract_main_content(OutputLevel::Standard, facts).unwrap();
    assert_eq!(result.selected_region_id, Some(20));
    assert_eq!(
        result.blocks.iter().map(block_text).collect::<Vec<_>>(),
        vec!["left top", "left bottom", "right top"]
    );
    assert_eq!(result.exclusions.non_content_region, 1);
}

#[test]
fn ct_003_excludes_hidden_cross_origin_sensitive_and_dangerous_urls() {
    let mut hidden = paragraph(1, 1, RegionKind::Main, "hidden secret");
    hidden.visible = false;
    let mut cross_origin = paragraph(2, 1, RegionKind::Main, "foreign frame");
    cross_origin.same_origin = false;
    let mut password = paragraph(3, 1, RegionKind::Main, "password-value");
    password.privacy = PrivacyClass::SensitiveControl;
    let dangerous = SourceFact {
        node_id: 4,
        region_id: 1,
        region_kind: RegionKind::Main,
        reading_key: key(0, 0, 4, 4),
        visible: true,
        same_origin: true,
        privacy: PrivacyClass::Public,
        content: SourceContent::Link {
            href: "javascript:alert(1)".to_owned(),
            text: "unsafe".to_owned(),
        },
    };
    let result = extract_main_content(
        OutputLevel::Standard,
        vec![
            hidden,
            cross_origin,
            password,
            dangerous,
            paragraph(5, 1, RegionKind::Main, "safe"),
        ],
    )
    .unwrap();
    assert_eq!(result.blocks.len(), 1);
    assert_eq!(block_text(&result.blocks[0]), "safe");
    assert_eq!(result.exclusions.hidden, 1);
    assert_eq!(result.exclusions.cross_origin, 1);
    assert_eq!(result.exclusions.sensitive, 1);
    assert_eq!(result.exclusions.unsafe_or_invalid, 1);
}

#[test]
fn ct_004_empty_and_navigation_only_pages_are_explicitly_empty() {
    let empty = extract_main_content(OutputLevel::Standard, Vec::new()).unwrap();
    assert_eq!(empty.selected_region_id, None);
    assert!(empty.blocks.is_empty());
    assert_eq!(empty.exclusions.total(), 0);

    let navigation = extract_main_content(
        OutputLevel::Standard,
        vec![paragraph(1, 8, RegionKind::Navigation, "one two three")],
    )
    .unwrap();
    assert_eq!(navigation.selected_region_id, None);
    assert!(navigation.blocks.is_empty());
    assert_eq!(navigation.exclusions.non_content_region, 1);

    let unknown_link_navigation = SourceFact {
        node_id: 2,
        region_id: 9,
        region_kind: RegionKind::Unknown,
        reading_key: key(0, 0, 0, 0),
        visible: true,
        same_origin: true,
        privacy: PrivacyClass::Public,
        content: SourceContent::Link {
            href: "https://example.test/topics".to_owned(),
            text: "topics".to_owned(),
        },
    };
    let unknown =
        extract_main_content(OutputLevel::Standard, vec![unknown_link_navigation]).unwrap();
    assert_eq!(unknown.selected_region_id, None);
    assert!(unknown.blocks.is_empty());
}

#[test]
fn ct_004_duplicate_node_keeps_earliest_reading_position() {
    let mut later = paragraph(7, 1, RegionKind::Main, "later duplicate");
    later.reading_key = key(0, 1, 0, 0);
    let mut earlier = paragraph(7, 1, RegionKind::Main, "earlier canonical");
    earlier.reading_key = key(0, 0, 0, 9);
    let result = extract_main_content(OutputLevel::Standard, vec![later, earlier]).unwrap();
    assert_eq!(result.blocks.len(), 1);
    assert_eq!(block_text(&result.blocks[0]), "earlier canonical");
    assert_eq!(result.exclusions.duplicate, 1);
}

#[test]
fn ct_004_infinite_list_is_bounded_with_explicit_omissions() {
    let facts = (0..STANDARD_MAX_SOURCE_FACTS + 19)
        .map(|index| SourceFact {
            node_id: index as u64,
            region_id: 1,
            region_kind: RegionKind::Main,
            reading_key: key(0, 0, index as u32, index as u32),
            visible: true,
            same_origin: true,
            privacy: PrivacyClass::Public,
            content: SourceContent::ListItem {
                depth: 0,
                ordinal: None,
                text: format!("item {index}"),
            },
        })
        .collect();
    let result = extract_main_content(OutputLevel::Standard, facts).unwrap();
    assert_eq!(result.blocks.len(), STANDARD_MAX_SOURCE_FACTS);
    assert_eq!(result.exclusions.over_budget, 19);
    assert!(result.exclusions.omitted_bytes > 0);
}

#[test]
fn compact_and_standard_use_distinct_source_budgets() {
    let facts: Vec<_> = (0..COMPACT_MAX_SOURCE_FACTS + 1)
        .map(|index| paragraph(index as u64, 1, RegionKind::Main, "content"))
        .collect();
    let compact = extract_main_content(OutputLevel::Compact, facts.clone()).unwrap();
    let standard = extract_main_content(OutputLevel::Standard, facts).unwrap();
    assert_eq!(compact.blocks.len(), COMPACT_MAX_SOURCE_FACTS);
    assert_eq!(compact.exclusions.over_budget, 1);
    assert_eq!(standard.blocks.len(), COMPACT_MAX_SOURCE_FACTS + 1);
}

#[test]
fn region_ties_choose_lowest_stable_region_id() {
    let result = extract_main_content(
        OutputLevel::Standard,
        vec![
            paragraph(1, 9, RegionKind::Article, "same"),
            paragraph(2, 4, RegionKind::Article, "same"),
        ],
    )
    .unwrap();
    assert_eq!(result.selected_region_id, Some(4));
    assert_eq!(result.exclusions.non_content_region, 1);
}

#[test]
fn all_nine_snapshot_shapes_are_preserved() {
    let contents = vec![
        SourceContent::Heading {
            level: 2,
            text: "h".into(),
        },
        SourceContent::Paragraph { text: "p".into() },
        SourceContent::ListItem {
            depth: 1,
            ordinal: Some(1),
            text: "li".into(),
        },
        SourceContent::Link {
            href: "https://example.test/x".into(),
            text: "a".into(),
        },
        SourceContent::Quote { text: "q".into() },
        SourceContent::CodeBlock {
            language: Some("rust".into()),
            text: "fn main() {}".into(),
        },
        SourceContent::Image {
            src: "https://example.test/i.png".into(),
            alt: "img".into(),
        },
        SourceContent::Table {
            rows: vec![TableRow {
                cells: vec!["cell".into()],
            }],
        },
        SourceContent::Divider,
    ];
    let facts = contents
        .into_iter()
        .enumerate()
        .map(|(index, content)| SourceFact {
            node_id: index as u64,
            region_id: 1,
            region_kind: RegionKind::Main,
            reading_key: key(0, 0, index as u32, index as u32),
            visible: true,
            same_origin: true,
            privacy: PrivacyClass::Public,
            content,
        })
        .collect();
    let result = extract_main_content(OutputLevel::Standard, facts).unwrap();
    assert_eq!(result.blocks.len(), 9);
}

#[test]
fn malformed_shapes_are_omitted_without_panicking() {
    let malformed = vec![
        SourceContent::Heading {
            level: 7,
            text: "bad".into(),
        },
        SourceContent::ListItem {
            depth: 9,
            ordinal: Some(0),
            text: "bad".into(),
        },
        SourceContent::Table { rows: vec![] },
        SourceContent::CodeBlock {
            language: Some("bad lang".into()),
            text: "x".into(),
        },
    ];
    let mut facts: Vec<_> = malformed
        .into_iter()
        .enumerate()
        .map(|(index, content)| SourceFact {
            node_id: index as u64,
            region_id: 1,
            region_kind: RegionKind::Main,
            reading_key: key(0, 0, index as u32, index as u32),
            visible: true,
            same_origin: true,
            privacy: PrivacyClass::Public,
            content,
        })
        .collect();
    facts.push(paragraph(99, 1, RegionKind::Main, "valid"));
    let result = extract_main_content(OutputLevel::Standard, facts).unwrap();
    assert_eq!(result.blocks.len(), 1);
    assert_eq!(result.exclusions.unsafe_or_invalid, 4);
}

#[test]
fn selected_region_respects_total_text_budget() {
    let chunk = "x".repeat(OutputLevel::Compact.max_block_text_bytes());
    let count = OutputLevel::Compact.max_total_text_bytes() / chunk.len() + 1;
    let facts = (0..count)
        .map(|index| paragraph(index as u64, 1, RegionKind::Main, &chunk))
        .collect();
    let result = extract_main_content(OutputLevel::Compact, facts).unwrap();
    assert_eq!(
        result.blocks.len(),
        OutputLevel::Compact.max_total_text_bytes() / chunk.len()
    );
    assert_eq!(result.exclusions.over_budget, 1);
    assert_eq!(result.exclusions.omitted_bytes, chunk.len() as u64);
}
