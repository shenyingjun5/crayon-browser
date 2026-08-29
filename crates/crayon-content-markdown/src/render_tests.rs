use super::*;
use crayon_page_data::{NavigationBinding, TableRow, TruncationInfo};

fn snapshot(level: OutputLevel, blocks: Vec<ContentBlock>) -> PageSnapshot {
    PageSnapshot::new(
        level,
        NavigationBinding::new(
            TabId::new("markdown-tab").unwrap(),
            SessionGeneration::from_raw(7),
        ),
        "https://example.test/article?private=1".to_owned(),
        "Article".to_owned(),
        12,
        TruncationInfo::default(),
        blocks,
    )
    .unwrap()
}

fn all_blocks() -> Vec<ContentBlock> {
    vec![
        ContentBlock::Heading {
            level: 2,
            text: "标题 *literal*".to_owned(),
        },
        ContentBlock::Paragraph {
            text: "Hello [world]\n<script>alert(1)</script>".to_owned(),
        },
        ContentBlock::ListItem {
            depth: 1,
            ordinal: None,
            text: "item\ncontinued".to_owned(),
        },
        ContentBlock::Link {
            href: "https://example.test/path?q=secret".to_owned(),
            text: "safe link".to_owned(),
        },
        ContentBlock::Quote {
            text: "quoted\nline".to_owned(),
        },
        ContentBlock::CodeBlock {
            language: Some("rust".to_owned()),
            text: "fn main() {\n    println!(\"```\");\n}".to_owned(),
        },
        ContentBlock::Image {
            src: "https://example.test/image.png?token=hidden".to_owned(),
            alt: "image [alt]".to_owned(),
        },
        ContentBlock::Table {
            rows: vec![
                TableRow {
                    cells: vec!["a|b".to_owned(), "c".to_owned()],
                },
                TableRow {
                    cells: vec!["1".to_owned(), "2".to_owned()],
                },
            ],
        },
        ContentBlock::Divider,
    ]
}

#[test]
fn ct_005_basic_golden_is_byte_stable() {
    let document = render_basic_snapshot(&snapshot(OutputLevel::Standard, all_blocks())).unwrap();
    let expected = include_str!("../tests/golden/basic.md");
    assert_eq!(document.markdown(), expected);
    assert_eq!(expected, include_str!("../tests/golden/previous/basic.md"));
    assert_eq!(document.tab_id().as_str(), "markdown-tab");
    assert_eq!(document.generation(), SessionGeneration::from_raw(7));
    assert_eq!(document.revision(), 12);
}

#[test]
fn ct_003_html_and_all_markdown_punctuation_are_escaped() {
    let punctuation = r#"\`*_{ }[]<>()#+-.!|"#.replace(' ', "");
    let document = render_snapshot(&snapshot(
        OutputLevel::Standard,
        vec![ContentBlock::Paragraph {
            text: format!("<script>{punctuation}</script>"),
        }],
    ))
    .unwrap();
    assert!(!document.markdown().contains("<script>"));
    assert!(document.markdown().starts_with("\\<script\\>"));
    assert!(document.markdown().ends_with('\n'));
}

#[test]
fn empty_snapshot_is_empty_and_repeat_render_is_identical() {
    let empty = snapshot(OutputLevel::Compact, Vec::new());
    assert_eq!(render_snapshot(&empty).unwrap().markdown(), "");
    let populated = snapshot(OutputLevel::Standard, all_blocks());
    assert_eq!(render_snapshot(&populated), render_snapshot(&populated));
}

#[test]
fn output_budget_fails_without_returning_partial_document() {
    let text = "*".repeat(OutputLevel::Compact.max_block_text_bytes());
    let blocks = (0..OutputLevel::Compact.max_total_text_bytes() / text.len())
        .map(|_| ContentBlock::Paragraph { text: text.clone() })
        .collect();
    let value = snapshot(OutputLevel::Compact, blocks);
    assert_eq!(render_snapshot(&value), Err(MarkdownError::OutputTooLarge));
}

#[test]
fn nonempty_output_has_exactly_one_terminal_lf() {
    let document = render_snapshot(&snapshot(
        OutputLevel::Standard,
        vec![ContentBlock::Paragraph {
            text: "line\n\n".to_owned(),
        }],
    ))
    .unwrap();
    assert_eq!(document.markdown(), "line\n");
}

#[test]
fn empty_image_alt_does_not_create_spurious_separators() {
    let document = render_snapshot(&snapshot(
        OutputLevel::Standard,
        vec![
            ContentBlock::Image {
                src: "https://example.test/image.png".to_owned(),
                alt: String::new(),
            },
            ContentBlock::Paragraph {
                text: "visible".to_owned(),
            },
        ],
    ))
    .unwrap();
    assert_eq!(document.markdown(), "visible\n");
}

#[test]
fn ct_005_006_normalized_golden_is_stable_and_private_url_parts_are_removed() {
    let document = render_snapshot(&snapshot(OutputLevel::Standard, all_blocks())).unwrap();
    assert_eq!(
        document.markdown(),
        include_str!("../tests/golden/normalized.md")
    );
    assert!(!document.markdown().contains("secret"));
    assert!(!document.markdown().contains("token"));
}

#[test]
fn nested_ordered_lists_and_dynamic_fences_are_deterministic() {
    let value = snapshot(
        OutputLevel::Standard,
        vec![
            ContentBlock::ListItem {
                depth: 3,
                ordinal: Some(12),
                text: "first\ncontinuation".to_owned(),
            },
            ContentBlock::CodeBlock {
                language: Some("rust".to_owned()),
                text: "```` inside".to_owned(),
            },
        ],
    );
    let rendered = render_snapshot(&value).unwrap();
    assert!(rendered
        .markdown()
        .starts_with("    12. first\n        continuation"));
    assert!(rendered
        .markdown()
        .contains("`````rust\n```` inside\n`````"));
}

#[test]
fn normalized_render_repeats_references_without_loading_or_dedup_state() {
    let link = ContentBlock::Link {
        href: "https://example.test/a?q=1#x".to_owned(),
        text: "same".to_owned(),
    };
    let value = snapshot(OutputLevel::Standard, vec![link.clone(), link]);
    assert_eq!(
        render_snapshot(&value).unwrap().markdown(),
        "[same](https://example.test/a)\n\n[same](https://example.test/a)\n"
    );
}
