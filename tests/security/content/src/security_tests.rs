use crayon_agent_gateway::grant::ProfileScope;
use crayon_agent_gateway::tools::content::{
    ContentReadPort, ContentReadRejection, MAX_SELECTION_BYTES,
};
use crayon_app_runtime::page_snapshot_runtime::{ContentPublishError, PageSnapshotRuntime};
use crayon_content_extract::{
    extract_main_content, PrivacyClass, ReadingKey, RegionKind, SourceContent, SourceFact,
};
use crayon_domain::{AgentTarget, SessionGeneration, TabId};
use crayon_page_data::{
    NavigationBinding, OutputLevel, PageSnapshot, SnapshotError, TruncationInfo, MAX_CACHED_TABS,
};

fn tab(raw: &str) -> TabId {
    TabId::new(raw).unwrap()
}

fn snapshot(tab_id: TabId, generation: u64, body: &str) -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(tab_id, SessionGeneration::from_raw(generation)),
        "https://example.test/security".to_owned(),
        "Security fixture".to_owned(),
        1,
        TruncationInfo::default(),
        vec![crayon_page_data::ContentBlock::Paragraph {
            text: body.to_owned(),
        }],
    )
    .unwrap()
}

#[test]
fn forged_provenance_unknown_fields_and_dangerous_urls_are_rejected() {
    let original = snapshot(tab("schema-tab"), 1, "body");
    let mut value = serde_json::to_value(&original).unwrap();
    value["provenance"]["verified_by"] = serde_json::json!("renderer");
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());

    let mut value = serde_json::to_value(&original).unwrap();
    value["unexpected"] = serde_json::json!(true);
    assert!(serde_json::from_value::<PageSnapshot>(value).is_err());

    for url in [
        "javascript:alert(1)",
        "data:text/html,secret",
        "file:///C:/secret",
        "https://user:password@example.test/private",
    ] {
        assert_eq!(
            PageSnapshot::new(
                OutputLevel::Standard,
                NavigationBinding::new(tab("url-tab"), SessionGeneration::from_raw(1)),
                url.to_owned(),
                "title".to_owned(),
                1,
                TruncationInfo::default(),
                Vec::new(),
            ),
            Err(SnapshotError::InvalidUrl)
        );
    }
}

#[test]
fn hostile_normalized_facts_never_release_sensitive_content_or_panic() {
    let mut seed = 0x517c_c1b7_u64;
    let mut facts = Vec::new();
    for index in 0..2_000_u64 {
        seed = seed.wrapping_mul(6_364_136_223_846_793_005).wrapping_add(1);
        let hidden = seed & 1 != 0;
        let cross_origin = seed & 2 != 0;
        let sensitive = seed & 4 != 0;
        facts.push(SourceFact {
            node_id: index,
            region_id: 1,
            region_kind: RegionKind::Main,
            reading_key: ReadingKey {
                section: 0,
                column: 0,
                row: index as u32,
                source_order: index as u32,
            },
            visible: !hidden,
            same_origin: !cross_origin,
            privacy: if sensitive {
                PrivacyClass::SensitiveControl
            } else {
                PrivacyClass::Public
            },
            content: SourceContent::Paragraph {
                text: format!("secret-{index}"),
            },
        });
    }
    let result = extract_main_content(OutputLevel::Standard, facts).unwrap();
    for block in result.blocks {
        let crayon_page_data::ContentBlock::Paragraph { text } = block else {
            panic!("fixture only emits paragraphs");
        };
        let index: u64 = text.trim_start_matches("secret-").parse().unwrap();
        let mut replay = 0x517c_c1b7_u64;
        for _ in 0..=index {
            replay = replay
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1);
        }
        assert_eq!(
            replay & 7,
            0,
            "only visible same-origin public facts survive"
        );
    }
}

#[test]
fn cross_profile_background_stale_and_capacity_paths_fail_closed() {
    let runtime = PageSnapshotRuntime::default();
    let scope_a = ProfileScope::new("profile-a").unwrap();
    let scope_b = ProfileScope::new("profile-b").unwrap();
    runtime
        .publish_content(
            scope_a.clone(),
            true,
            String::new(),
            snapshot(tab("tab-0"), 1, "body-0"),
        )
        .unwrap();
    runtime
        .publish_content(
            scope_a.clone(),
            true,
            String::new(),
            snapshot(tab("tab-1"), 1, "body-1"),
        )
        .unwrap();
    let background = AgentTarget::Tab { tab: tab("tab-0") };
    assert_eq!(
        runtime.get_snapshot(&scope_a, &background, SessionGeneration::from_raw(1)),
        Err(ContentReadRejection::BackgroundTarget)
    );
    let active = AgentTarget::Tab { tab: tab("tab-1") };
    assert_eq!(
        runtime.get_snapshot(&scope_b, &active, SessionGeneration::from_raw(1)),
        Err(ContentReadRejection::TargetInvalid)
    );
    assert_eq!(
        runtime.get_snapshot(&scope_a, &active, SessionGeneration::from_raw(0)),
        Err(ContentReadRejection::StaleGeneration)
    );

    for index in 2..MAX_CACHED_TABS {
        runtime
            .publish_content(
                scope_a.clone(),
                false,
                String::new(),
                snapshot(tab(&format!("tab-{index}")), 1, "body"),
            )
            .unwrap();
    }
    assert_eq!(
        runtime.publish_content(
            scope_a,
            false,
            String::new(),
            snapshot(tab("overflow-tab"), 1, "body"),
        ),
        Err(ContentPublishError::Rejected(
            ContentReadRejection::CapacityExceeded
        ))
    );
}

#[test]
fn oversized_selection_is_rejected_before_snapshot_state_changes() {
    let runtime = PageSnapshotRuntime::default();
    let scope = ProfileScope::new("profile-a").unwrap();
    assert_eq!(
        runtime.publish_content(
            scope.clone(),
            true,
            "x".repeat(MAX_SELECTION_BYTES + 1),
            snapshot(tab("selection-tab"), 1, "body"),
        ),
        Err(ContentPublishError::Rejected(
            ContentReadRejection::SelectionTooLarge
        ))
    );
    assert_eq!(runtime.list_targets(&scope).unwrap(), Vec::new());
    assert_eq!(runtime.stats().cached_tabs, 0);
}
