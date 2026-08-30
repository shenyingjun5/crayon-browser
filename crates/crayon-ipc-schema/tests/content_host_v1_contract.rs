use crayon_ipc_schema::{
    decode_content_host_message, encode_content_host_message, ContentHostEngineErrorCode,
    ContentHostError, ContentHostErrorCode, ContentHostFact, ContentHostFactKind,
    ContentHostMessage, ContentHostMode, ContentHostTerminalStatus, MAX_CONTENT_HOST_FRAME_BYTES,
    MAX_CONTENT_HOST_MARKDOWN_BYTES,
};
use serde::Deserialize;
use std::path::PathBuf;

#[derive(Deserialize)]
struct Golden {
    vectors: Vec<GoldenVector>,
}
#[derive(Deserialize)]
struct GoldenVector {
    name: String,
    hex: String,
}

fn base(kind: ContentHostFactKind, text: &str) -> ContentHostFact {
    ContentHostFact {
        kind,
        text: text.into(),
        url: None,
        language: None,
        level: 0,
        depth: 0,
        ordered: false,
        ordinal: None,
        table_columns: 0,
        table_cells: vec![],
    }
}

fn messages() -> Vec<(&'static str, ContentHostMessage)> {
    let mut heading = base(ContentHostFactKind::Heading, "Heading");
    heading.level = 2;
    let paragraph = base(ContentHostFactKind::Paragraph, "Paragraph");
    let mut list = base(ContentHostFactKind::ListItem, "Item");
    list.depth = 2;
    list.ordered = true;
    list.ordinal = Some(3);
    let mut link = base(ContentHostFactKind::Link, "Link");
    link.url = Some("https://example.test/a".into());
    let mut image = base(ContentHostFactKind::Image, "Alt");
    image.url = Some("https://example.test/i.png".into());
    let mut table = base(ContentHostFactKind::Table, "");
    table.table_columns = 2;
    table.table_cells = vec!["A".into(), "B".into(), "1".into(), "2".into()];
    let mut code = base(ContentHostFactKind::CodeBlock, "let x = 1;\n");
    code.language = Some("rust".into());
    let divider = base(ContentHostFactKind::Divider, "");
    let quote = base(ContentHostFactKind::Quote, "Quote");
    vec![
        (
            "begin",
            ContentHostMessage::Begin {
                request_id: "req-1".into(),
                tab_id: "tab-1".into(),
                navigation_id: 7,
                generation: 11,
                mode: ContentHostMode::Standard,
                url: "https://example.test/".into(),
                title: "Example".into(),
            },
        ),
        (
            "fact_batch",
            ContentHostMessage::FactBatch {
                request_id: "req-1".into(),
                tab_id: "tab-1".into(),
                navigation_id: 7,
                generation: 11,
                sequence: 0,
                facts: vec![
                    heading, paragraph, list, link, image, table, code, divider, quote,
                ],
            },
        ),
        (
            "terminal",
            ContentHostMessage::Terminal {
                request_id: "req-1".into(),
                tab_id: "tab-1".into(),
                navigation_id: 7,
                generation: 11,
                status: ContentHostTerminalStatus::Completed,
                error: ContentHostEngineErrorCode::None,
            },
        ),
        (
            "terminal_error",
            ContentHostMessage::Terminal {
                request_id: "req-1".into(),
                tab_id: "tab-1".into(),
                navigation_id: 7,
                generation: 11,
                status: ContentHostTerminalStatus::Rejected,
                error: ContentHostEngineErrorCode::CapacityExceeded,
            },
        ),
        (
            "cancel",
            ContentHostMessage::Cancel {
                request_id: "req-2".into(),
            },
        ),
        (
            "navigation",
            ContentHostMessage::Navigation {
                tab_id: "tab-1".into(),
                navigation_id: 8,
                generation: 12,
            },
        ),
        (
            "close_tab",
            ContentHostMessage::CloseTab {
                tab_id: "tab-1".into(),
            },
        ),
        ("shutdown", ContentHostMessage::Shutdown),
        (
            "markdown_chunk",
            ContentHostMessage::MarkdownChunk {
                request_id: "req-1".into(),
                tab_id: "tab-1".into(),
                navigation_id: 7,
                generation: 11,
                sequence: 1,
                completed: true,
                markdown: "# Example\n".into(),
            },
        ),
        (
            "error_reply",
            ContentHostMessage::ErrorReply {
                request_id: "req-2".into(),
                code: ContentHostErrorCode::Cancelled,
            },
        ),
    ]
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn golden(set: &str) -> Golden {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../schemas")
        .join(set)
        .join("content_host_v1.json");
    serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap()
}

#[test]
fn cnt_18a_rust_roundtrip_and_cross_language_golden() {
    let actual: Vec<_> = messages()
        .iter()
        .map(|(name, message)| {
            let bytes = encode_content_host_message(message).unwrap();
            assert_eq!(decode_content_host_message(&bytes).unwrap(), *message);
            ((*name).to_owned(), hex(&bytes))
        })
        .collect();
    for set in ["current", "previous"] {
        let expected: Vec<_> = golden(set)
            .vectors
            .into_iter()
            .map(|v| (v.name, v.hex))
            .collect();
        assert_eq!(
            actual, expected,
            "{set} content-host golden mismatch; actual={actual:?}"
        );
    }
}

#[test]
fn cnt_18a_bounds_and_hostile_mutations_fail_closed() {
    let mut zero = messages()[0].1.clone();
    if let ContentHostMessage::Begin { navigation_id, .. } = &mut zero {
        *navigation_id = 0;
    }
    assert_eq!(
        encode_content_host_message(&zero),
        Err(ContentHostError::InvalidValue)
    );
    let huge = ContentHostMessage::MarkdownChunk {
        request_id: "r".into(),
        tab_id: "t".into(),
        navigation_id: 1,
        generation: 1,
        sequence: 0,
        completed: true,
        markdown: "x".repeat(MAX_CONTENT_HOST_MARKDOWN_BYTES + 1),
    };
    assert_eq!(
        encode_content_host_message(&huge),
        Err(ContentHostError::LengthExceeded)
    );
    assert_eq!(
        decode_content_host_message(&vec![0; MAX_CONTENT_HOST_FRAME_BYTES + 1]),
        Err(ContentHostError::FrameTooLarge)
    );
    let max_id = ContentHostMessage::Cancel {
        request_id: "r".repeat(128),
    };
    let encoded = encode_content_host_message(&max_id).unwrap();
    assert_eq!(decode_content_host_message(&encoded).unwrap(), max_id);
    assert!(encode_content_host_message(&ContentHostMessage::Cancel {
        request_id: "request.with:scope".into(),
    })
    .is_ok());
    assert_eq!(
        encode_content_host_message(&ContentHostMessage::CloseTab {
            tab_id: "tab.with.dot".into(),
        }),
        Err(ContentHostError::InvalidValue)
    );

    for (_, message) in messages() {
        let bytes = encode_content_host_message(&message).unwrap();
        for cut in 0..bytes.len() {
            assert!(decode_content_host_message(&bytes[..cut]).is_err());
        }
        let mut trailing = bytes.clone();
        trailing.push(0);
        assert_eq!(
            decode_content_host_message(&trailing),
            Err(ContentHostError::TrailingBytes)
        );
        let mut bad_version = bytes.clone();
        bad_version[5] = 2;
        assert_eq!(
            decode_content_host_message(&bad_version),
            Err(ContentHostError::UnsupportedVersion)
        );
        let mut bad_flags = bytes;
        bad_flags[7] = 1;
        assert_eq!(
            decode_content_host_message(&bad_flags),
            Err(ContentHostError::InvalidFlags)
        );
    }
}

#[test]
fn cnt_18a_unknown_enum_and_invalid_utf8_are_rejected() {
    let mut unknown = encode_content_host_message(&ContentHostMessage::Shutdown).unwrap();
    unknown[6] = 0xff;
    assert_eq!(
        decode_content_host_message(&unknown),
        Err(ContentHostError::UnknownKind)
    );
    let mut fact_batch = encode_content_host_message(&messages()[1].1).unwrap();
    fact_batch[48] = 0xff;
    assert_eq!(
        decode_content_host_message(&fact_batch),
        Err(ContentHostError::InvalidValue)
    );
    let mut cancel = encode_content_host_message(&ContentHostMessage::Cancel {
        request_id: "r".into(),
    })
    .unwrap();
    *cancel.last_mut().unwrap() = 0xff;
    assert_eq!(
        decode_content_host_message(&cancel),
        Err(ContentHostError::InvalidUtf8)
    );
}
