use crayon_agent_gateway::grant::{GrantKind, GrantManager, GrantRequest, ProfileScope};
use crayon_agent_gateway::tools::content::{
    ContentReadError, ContentReadPort, ContentReadRejection, ContentReader,
    MAX_SNAPSHOT_OUTPUT_BYTES,
};
use crayon_app_runtime::page_snapshot_runtime::PageSnapshotRuntime;
use crayon_content_extract::{
    extract_main_content, PrivacyClass, ReadingKey, RegionKind, SourceContent, SourceFact,
};
use crayon_domain::{AgentCapability, AgentTarget, SessionGeneration, TabId};
use crayon_page_data::{NavigationBinding, OutputLevel, PageSnapshot, TruncationInfo};

const SESSION: &str = "cnt-e2e-client";

fn profile(raw: &str) -> ProfileScope {
    ProfileScope::new(raw).expect("profile")
}

fn tab(raw: &str) -> TabId {
    TabId::new(raw).expect("tab")
}

fn fact(
    node_id: u64,
    content: SourceContent,
    visible: bool,
    same_origin: bool,
    privacy: PrivacyClass,
) -> SourceFact {
    SourceFact {
        node_id,
        region_id: 7,
        region_kind: RegionKind::Article,
        reading_key: ReadingKey {
            section: 0,
            column: 0,
            row: node_id as u32,
            source_order: node_id as u32,
        },
        visible,
        same_origin,
        privacy,
        content,
    }
}

fn make_snapshot(
    tab_id: TabId,
    generation: u64,
    revision: u64,
    facts: Vec<SourceFact>,
) -> PageSnapshot {
    let extracted = extract_main_content(OutputLevel::Standard, facts).expect("extract");
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(tab_id, SessionGeneration::from_raw(generation)),
        "https://example.test/article?private=query#section".to_owned(),
        "C1 article".to_owned(),
        revision,
        TruncationInfo::default(),
        extracted.blocks,
    )
    .expect("snapshot")
}

fn grant(manager: &mut GrantManager, scope: ProfileScope) {
    manager
        .issue(
            GrantRequest {
                kind: GrantKind::AppSession,
                session: SESSION.to_owned(),
                profile: scope,
                capability: AgentCapability::PageRead,
                target: None,
                task: None,
                ttl_ms: 60_000,
            },
            1,
        )
        .expect("grant");
}

#[test]
fn ct_001_to_006_full_pipeline_is_deterministic_and_private() {
    let visible = vec![
        fact(
            1,
            SourceContent::Heading {
                level: 1,
                text: "Visible heading".to_owned(),
            },
            true,
            true,
            PrivacyClass::Public,
        ),
        fact(
            2,
            SourceContent::Paragraph {
                text: "Visible paragraph".to_owned(),
            },
            true,
            true,
            PrivacyClass::Public,
        ),
        fact(
            3,
            SourceContent::Link {
                href: "https://example.test/reference?token=secret#part".to_owned(),
                text: "Reference".to_owned(),
            },
            true,
            true,
            PrivacyClass::Public,
        ),
        fact(
            4,
            SourceContent::Paragraph {
                text: "hidden-value".to_owned(),
            },
            false,
            true,
            PrivacyClass::Public,
        ),
        fact(
            5,
            SourceContent::Paragraph {
                text: "cross-origin-value".to_owned(),
            },
            true,
            false,
            PrivacyClass::Public,
        ),
        fact(
            6,
            SourceContent::Paragraph {
                text: "password-value".to_owned(),
            },
            true,
            true,
            PrivacyClass::SensitiveControl,
        ),
    ];
    let runtime = PageSnapshotRuntime::default();
    let scope = profile("profile-a");
    runtime
        .publish_content(
            scope.clone(),
            true,
            "Visible paragraph".to_owned(),
            make_snapshot(tab("tab-a"), 1, 1, visible),
        )
        .expect("publish");
    let mut grants = GrantManager::new();
    grant(&mut grants, scope.clone());
    let mut reader = ContentReader::new(&mut grants, &runtime);
    assert_eq!(reader.list_targets(SESSION, &scope, 2).unwrap().len(), 1);
    let target = AgentTarget::ActiveTab;
    let snapshot = reader
        .get_snapshot(
            SESSION,
            &scope,
            &target,
            SessionGeneration::from_raw(1),
            MAX_SNAPSHOT_OUTPUT_BYTES,
            2,
        )
        .unwrap();
    assert_eq!(snapshot.blocks().len(), 3);
    let markdown = reader
        .get_markdown(
            SESSION,
            &scope,
            &target,
            SessionGeneration::from_raw(1),
            4096,
            2,
        )
        .unwrap();
    assert_eq!(
        markdown.markdown(),
        "# Visible heading\n\nVisible paragraph\n\n[Reference](https://example.test/reference)\n"
    );
    for forbidden in [
        "hidden-value",
        "cross-origin-value",
        "password-value",
        "token=secret",
    ] {
        assert!(!markdown.markdown().contains(forbidden));
    }
}

#[test]
fn ct_004_to_006_empty_and_long_pages_remain_bounded() {
    let empty = make_snapshot(tab("empty-tab"), 1, 1, Vec::new());
    assert!(empty.blocks().is_empty());
    let long: Vec<_> = (0..600)
        .map(|index| {
            fact(
                index,
                SourceContent::Paragraph {
                    text: format!("paragraph-{index}-{}", "x".repeat(128)),
                },
                true,
                true,
                PrivacyClass::Public,
            )
        })
        .collect();
    let first = make_snapshot(tab("long-tab"), 1, 1, long.clone());
    let second = make_snapshot(tab("long-tab"), 1, 1, long);
    assert_eq!(first, second);
    assert_eq!(first.blocks().len(), 600);
}

#[test]
fn ct_007_008_navigation_close_profile_and_shutdown_release_old_content() {
    let runtime = PageSnapshotRuntime::default();
    let scope_a = profile("profile-a");
    let scope_b = profile("profile-b");
    let page_tab = tab("lifecycle-tab");
    runtime
        .publish_content(
            scope_a.clone(),
            true,
            String::new(),
            make_snapshot(
                page_tab.clone(),
                1,
                1,
                vec![fact(
                    1,
                    SourceContent::Paragraph {
                        text: "old body".to_owned(),
                    },
                    true,
                    true,
                    PrivacyClass::Public,
                )],
            ),
        )
        .unwrap();
    let read = runtime
        .begin_read(&page_tab, SessionGeneration::from_raw(1), 1)
        .unwrap();
    runtime
        .advance_navigation(page_tab.clone(), SessionGeneration::from_raw(2))
        .unwrap();
    assert_eq!(
        runtime.next_page(read),
        Err(crayon_page_data::OwnerError::StaleGeneration)
    );
    let target = AgentTarget::Tab {
        tab: page_tab.clone(),
    };
    assert_eq!(
        runtime.get_snapshot(&scope_a, &target, SessionGeneration::from_raw(1)),
        Err(ContentReadRejection::StaleGeneration)
    );
    assert_eq!(
        runtime.get_snapshot(&scope_b, &target, SessionGeneration::from_raw(2)),
        Err(ContentReadRejection::TargetInvalid)
    );
    runtime.close_tab(&page_tab).unwrap();
    assert_eq!(runtime.stats().cached_tabs, 0);
    runtime.shut_down();
    assert_eq!(runtime.list_targets(&scope_a).unwrap(), Vec::new());

    let mut grants = GrantManager::new();
    let mut reader = ContentReader::new(&mut grants, &runtime);
    assert!(matches!(
        reader.get_title(
            SESSION,
            &scope_a,
            &target,
            SessionGeneration::from_raw(2),
            2
        ),
        Err(ContentReadError::Grant(_))
    ));
}
