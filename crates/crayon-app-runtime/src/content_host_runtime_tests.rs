use crate::content_host_runtime::{
    ContentHostRuntime, ContentHostRuntimeError, MAX_ACTIVE_CONTENT_STREAMS,
};
use crayon_ipc_schema::{
    ContentHostEngineErrorCode, ContentHostErrorCode, ContentHostFact, ContentHostFactKind,
    ContentHostMessage, ContentHostMode, ContentHostTerminalStatus,
    MAX_CONTENT_HOST_MARKDOWN_BYTES,
};

fn fact(kind: ContentHostFactKind, text: &str) -> ContentHostFact {
    ContentHostFact {
        kind,
        text: text.to_owned(),
        url: None,
        language: None,
        level: 0,
        depth: 0,
        ordered: false,
        ordinal: None,
        table_columns: 0,
        table_cells: Vec::new(),
    }
}

fn begin(request: &str, tab: &str, navigation: u64, generation: u64) -> ContentHostMessage {
    ContentHostMessage::Begin {
        request_id: request.to_owned(),
        tab_id: tab.to_owned(),
        navigation_id: navigation,
        generation,
        mode: ContentHostMode::Standard,
        url: "https://example.test/article".to_owned(),
        title: "Example".to_owned(),
    }
}

fn batch(
    request: &str,
    tab: &str,
    navigation: u64,
    generation: u64,
    sequence: u32,
    facts: Vec<ContentHostFact>,
) -> ContentHostMessage {
    ContentHostMessage::FactBatch {
        request_id: request.to_owned(),
        tab_id: tab.to_owned(),
        navigation_id: navigation,
        generation,
        sequence,
        facts,
    }
}

fn terminal(
    request: &str,
    tab: &str,
    navigation: u64,
    generation: u64,
    status: ContentHostTerminalStatus,
) -> ContentHostMessage {
    ContentHostMessage::Terminal {
        request_id: request.to_owned(),
        tab_id: tab.to_owned(),
        navigation_id: navigation,
        generation,
        status,
        error: if status == ContentHostTerminalStatus::Completed {
            ContentHostEngineErrorCode::None
        } else {
            ContentHostEngineErrorCode::InvalidState
        },
    }
}

fn markdown(replies: &[ContentHostMessage]) -> String {
    replies
        .iter()
        .map(|message| match message {
            ContentHostMessage::MarkdownChunk { markdown, .. } => markdown.as_str(),
            other => panic!("unexpected reply: {other:?}"),
        })
        .collect()
}

#[test]
fn cnt_18b_all_facts_flow_through_extract_owner_and_markdown() {
    let mut host = ContentHostRuntime::default();
    host.handle(begin("req-1", "tab-1", 7, 11)).unwrap();
    let mut heading = fact(ContentHostFactKind::Heading, "Heading");
    heading.level = 2;
    let paragraph = fact(ContentHostFactKind::Paragraph, "Paragraph");
    let mut list = fact(ContentHostFactKind::ListItem, "Item");
    list.depth = 2;
    list.ordered = true;
    list.ordinal = Some(3);
    let mut link = fact(ContentHostFactKind::Link, "Link");
    link.url = Some("https://example.test/link".to_owned());
    let mut image = fact(ContentHostFactKind::Image, "Alt");
    image.url = Some("https://example.test/image.png".to_owned());
    let mut table = fact(ContentHostFactKind::Table, "");
    table.table_columns = 2;
    table.table_cells = ["A", "B", "1", "2"].map(str::to_owned).to_vec();
    let mut code = fact(ContentHostFactKind::CodeBlock, "let x = 1;\n");
    code.language = Some("rust".to_owned());
    let divider = fact(ContentHostFactKind::Divider, "");
    let quote = fact(ContentHostFactKind::Quote, "Quote");
    host.handle(batch(
        "req-1",
        "tab-1",
        7,
        11,
        0,
        vec![heading, paragraph, list, link],
    ))
    .unwrap();
    host.handle(batch(
        "req-1",
        "tab-1",
        7,
        11,
        1,
        vec![image, table, code, divider, quote],
    ))
    .unwrap();
    let replies = host
        .handle(terminal(
            "req-1",
            "tab-1",
            7,
            11,
            ContentHostTerminalStatus::Completed,
        ))
        .unwrap();
    let output = markdown(&replies);
    assert!(output.contains("## Heading"));
    assert!(output.contains("Paragraph"));
    assert!(output.contains("3. Item"));
    assert!(output.contains("[Link](https://example.test/link)"));
    assert!(output.contains("![Alt](https://example.test/image.png)"));
    assert!(output.contains("| A | B |"));
    assert!(output.contains("```rust"));
    assert!(output.contains("---"));
    assert!(output.contains("> Quote"));
    assert_eq!(host.active_streams(), 0);
}

#[test]
fn cnt_18b_empty_page_and_revision_replacement_are_supported() {
    let mut host = ContentHostRuntime::default();
    for request in ["empty-1", "empty-2"] {
        host.handle(begin(request, "tab-1", 1, 1)).unwrap();
        let replies = host
            .handle(terminal(
                request,
                "tab-1",
                1,
                1,
                ContentHostTerminalStatus::Completed,
            ))
            .unwrap();
        assert_eq!(markdown(&replies), "");
        assert!(matches!(
            replies.as_slice(),
            [ContentHostMessage::MarkdownChunk {
                sequence: 0,
                completed: true,
                ..
            }]
        ));
    }
}

#[test]
fn cnt_18b_markdown_is_utf8_safe_and_chunked() {
    let mut host = ContentHostRuntime::default();
    host.handle(begin("large", "tab-1", 1, 1)).unwrap();
    let facts = (0..5)
        .map(|_| fact(ContentHostFactKind::Paragraph, &"界".repeat(5_000)))
        .collect();
    host.handle(batch("large", "tab-1", 1, 1, 0, facts))
        .unwrap();
    let replies = host
        .handle(terminal(
            "large",
            "tab-1",
            1,
            1,
            ContentHostTerminalStatus::Completed,
        ))
        .unwrap();
    assert!(replies.len() > 1);
    for (index, reply) in replies.iter().enumerate() {
        let ContentHostMessage::MarkdownChunk {
            sequence,
            completed,
            markdown,
            ..
        } = reply
        else {
            panic!("unexpected reply")
        };
        assert_eq!(*sequence as usize, index);
        assert!(markdown.len() <= MAX_CONTENT_HOST_MARKDOWN_BYTES);
        assert_eq!(*completed, index + 1 == replies.len());
    }
}

#[test]
fn cnt_18b_sequence_and_target_mismatch_drop_partial_stream() {
    let mut host = ContentHostRuntime::default();
    host.handle(begin("req", "tab-1", 1, 1)).unwrap();
    assert_eq!(
        host.handle(batch(
            "req",
            "tab-1",
            1,
            1,
            1,
            vec![fact(ContentHostFactKind::Paragraph, "late")],
        )),
        Err(ContentHostRuntimeError::SequenceMismatch)
    );
    assert_eq!(host.active_streams(), 0);
    assert_eq!(
        host.handle(terminal(
            "req",
            "tab-1",
            1,
            1,
            ContentHostTerminalStatus::Completed,
        )),
        Err(ContentHostRuntimeError::NotFound)
    );

    host.handle(begin("req-2", "tab-1", 1, 1)).unwrap();
    assert_eq!(
        host.handle(batch(
            "req-2",
            "other-tab",
            1,
            1,
            0,
            vec![fact(ContentHostFactKind::Paragraph, "wrong")],
        )),
        Err(ContentHostRuntimeError::StaleNavigation)
    );
    assert_eq!(host.active_streams(), 0);
}

#[test]
fn cnt_18b_duplicate_and_capacity_guards_are_bounded() {
    let mut host = ContentHostRuntime::default();
    host.handle(begin("req-0", "tab-0", 1, 1)).unwrap();
    assert_eq!(
        host.handle(begin("req-0", "tab-1", 1, 1)),
        Err(ContentHostRuntimeError::DuplicateRequest)
    );
    assert_eq!(
        host.handle(begin("req-1", "tab-0", 1, 1)),
        Err(ContentHostRuntimeError::DuplicateTab)
    );
    for index in 1..MAX_ACTIVE_CONTENT_STREAMS {
        host.handle(begin(
            &format!("req-{index}"),
            &format!("tab-{index}"),
            1,
            1,
        ))
        .unwrap();
    }
    assert_eq!(host.active_streams(), MAX_ACTIVE_CONTENT_STREAMS);
    assert_eq!(
        host.handle(begin("overflow", "tab-overflow", 1, 1)),
        Err(ContentHostRuntimeError::CapacityExceeded)
    );
}

#[test]
fn cnt_18b_fact_and_mode_budgets_drop_partial_stream() {
    let mut host = ContentHostRuntime::default();
    host.handle(begin("wide", "tab-1", 1, 1)).unwrap();
    assert_eq!(
        host.handle(batch(
            "wide",
            "tab-1",
            1,
            1,
            0,
            (0..65)
                .map(|_| fact(ContentHostFactKind::Paragraph, "x"))
                .collect(),
        )),
        Err(ContentHostRuntimeError::CapacityExceeded)
    );
    assert_eq!(host.active_streams(), 0);

    let mut compact = begin("compact", "tab-1", 1, 1);
    if let ContentHostMessage::Begin { mode, .. } = &mut compact {
        *mode = ContentHostMode::Compact;
    }
    host.handle(compact).unwrap();
    host.handle(batch(
        "compact",
        "tab-1",
        1,
        1,
        0,
        (0..64)
            .map(|_| fact(ContentHostFactKind::Paragraph, &"x".repeat(2_048)))
            .collect(),
    ))
    .unwrap();
    assert_eq!(
        host.handle(batch(
            "compact",
            "tab-1",
            1,
            1,
            1,
            vec![fact(ContentHostFactKind::Paragraph, "overflow")],
        )),
        Err(ContentHostRuntimeError::CapacityExceeded)
    );

    let mut compact = begin("compact-count", "tab-1", 1, 1);
    if let ContentHostMessage::Begin { mode, .. } = &mut compact {
        *mode = ContentHostMode::Compact;
    }
    host.handle(compact).unwrap();
    for sequence in 0..8 {
        host.handle(batch(
            "compact-count",
            "tab-1",
            1,
            1,
            sequence,
            (0..64)
                .map(|_| fact(ContentHostFactKind::Paragraph, "x"))
                .collect(),
        ))
        .unwrap();
    }
    assert_eq!(
        host.handle(batch(
            "compact-count",
            "tab-1",
            1,
            1,
            8,
            vec![fact(ContentHostFactKind::Paragraph, "overflow")],
        )),
        Err(ContentHostRuntimeError::CapacityExceeded)
    );
    assert_eq!(host.active_streams(), 0);
}

#[test]
fn cnt_18b_terminal_cancel_navigation_close_and_shutdown_release_state() {
    let mut host = ContentHostRuntime::default();
    host.handle(begin("rejected", "tab-1", 1, 1)).unwrap();
    assert_eq!(
        host.handle(terminal(
            "rejected",
            "tab-1",
            1,
            1,
            ContentHostTerminalStatus::Rejected,
        ))
        .unwrap(),
        vec![ContentHostMessage::ErrorReply {
            request_id: "rejected".to_owned(),
            code: ContentHostErrorCode::InvalidMessage,
        }]
    );

    host.handle(begin("cancel", "tab-1", 1, 1)).unwrap();
    let cancelled = host
        .handle(ContentHostMessage::Cancel {
            request_id: "cancel".to_owned(),
        })
        .unwrap();
    assert!(matches!(
        cancelled.as_slice(),
        [ContentHostMessage::ErrorReply {
            code: ContentHostErrorCode::Cancelled,
            ..
        }]
    ));

    host.handle(begin("navigate", "tab-1", 1, 1)).unwrap();
    let stale = host
        .handle(ContentHostMessage::Navigation {
            tab_id: "tab-1".to_owned(),
            navigation_id: 2,
            generation: 2,
        })
        .unwrap();
    assert!(matches!(
        stale.as_slice(),
        [ContentHostMessage::ErrorReply {
            code: ContentHostErrorCode::StaleNavigation,
            ..
        }]
    ));
    assert_eq!(host.active_streams(), 0);

    host.handle(begin("close", "tab-1", 2, 2)).unwrap();
    host.handle(ContentHostMessage::CloseTab {
        tab_id: "tab-1".to_owned(),
    })
    .unwrap();
    assert_eq!(host.active_streams(), 0);

    host.handle(begin("shutdown", "tab-2", 1, 1)).unwrap();
    let replies = host.handle(ContentHostMessage::Shutdown).unwrap();
    assert!(matches!(
        replies.as_slice(),
        [ContentHostMessage::ErrorReply {
            code: ContentHostErrorCode::HostUnavailable,
            ..
        }]
    ));
    assert_eq!(host.handle(ContentHostMessage::Shutdown), Ok(Vec::new()));
    assert_eq!(
        host.handle(begin("late", "tab-3", 1, 1)),
        Err(ContentHostRuntimeError::ShutDown)
    );
}

#[test]
fn cnt_18b_stale_navigation_and_output_direction_fail_closed() {
    let mut host = ContentHostRuntime::default();
    host.handle(ContentHostMessage::Navigation {
        tab_id: "tab-1".to_owned(),
        navigation_id: 2,
        generation: 2,
    })
    .unwrap();
    assert_eq!(
        host.handle(begin("stale", "tab-1", 1, 1)),
        Err(ContentHostRuntimeError::StaleNavigation)
    );
    host.handle(begin("current", "tab-1", 2, 2)).unwrap();
    assert_eq!(
        host.handle(ContentHostMessage::Navigation {
            tab_id: "tab-1".to_owned(),
            navigation_id: 1,
            generation: 1,
        }),
        Err(ContentHostRuntimeError::StaleNavigation)
    );
    assert_eq!(host.active_streams(), 1);
    assert_eq!(
        host.handle(ContentHostMessage::Navigation {
            tab_id: "tab-1".to_owned(),
            navigation_id: 2,
            generation: 2,
        }),
        Ok(Vec::new())
    );
    assert_eq!(host.active_streams(), 1);
    assert_eq!(
        host.handle(ContentHostMessage::ErrorReply {
            request_id: "forged".to_owned(),
            code: ContentHostErrorCode::Cancelled,
        }),
        Err(ContentHostRuntimeError::InvalidMessage)
    );
    assert_eq!(
        ContentHostRuntimeError::CapacityExceeded.reply_code(),
        ContentHostErrorCode::CapacityExceeded
    );
}
