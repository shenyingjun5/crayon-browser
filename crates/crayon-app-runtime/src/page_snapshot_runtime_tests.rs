use super::*;
use crayon_agent_gateway::grant::ProfileScope;
use crayon_agent_gateway::tools::content::{ContentReadPort, ContentReadRejection};
use crayon_domain::AgentTarget;
use crayon_page_data::{ContentBlock, NavigationBinding, OutputLevel, TruncationInfo};
use std::sync::Arc;

fn tab() -> TabId {
    TabId::new("runtime-tab").unwrap()
}

fn snapshot(generation: u64, revision: u64) -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(tab(), SessionGeneration::from_raw(generation)),
        "https://example.test/runtime".to_owned(),
        "Runtime page".to_owned(),
        revision,
        TruncationInfo::default(),
        vec![
            ContentBlock::Heading {
                level: 1,
                text: "Heading".to_owned(),
            },
            ContentBlock::Paragraph {
                text: "Paragraph".to_owned(),
            },
        ],
    )
    .unwrap()
}

#[test]
fn runtime_owns_publish_pagination_cancel_and_navigation() {
    let runtime = PageSnapshotRuntime::default();
    assert_eq!(runtime.publish(snapshot(1, 1)), Ok(PublishResult::Stored));
    let read = runtime
        .begin_read(&tab(), SessionGeneration::from_raw(1), 1)
        .unwrap();
    let first = runtime.next_page(read).unwrap();
    assert_eq!(first.blocks().len(), 1);
    assert!(first.has_more());
    assert_eq!(runtime.cancel(read), Ok(true));
    assert_eq!(runtime.cancel(read), Ok(false));

    let stale = runtime
        .begin_read(&tab(), SessionGeneration::from_raw(1), 1)
        .unwrap();
    assert_eq!(
        runtime.advance_navigation(tab(), SessionGeneration::from_raw(2)),
        Ok(true)
    );
    assert_eq!(runtime.next_page(stale), Err(OwnerError::StaleGeneration));
    assert_eq!(
        runtime.publish(snapshot(1, 9)),
        Err(OwnerError::StaleGeneration)
    );
    assert_eq!(runtime.publish(snapshot(2, 1)), Ok(PublishResult::Stored));
}

#[test]
fn mutex_poison_recovers_and_shutdown_is_terminal() {
    let runtime = Arc::new(PageSnapshotRuntime::default());
    let poison = Arc::clone(&runtime);
    assert!(std::thread::spawn(move || {
        let _guard = poison.state.lock().unwrap();
        panic!("intentional poison");
    })
    .join()
    .is_err());
    assert_eq!(runtime.publish(snapshot(1, 1)), Ok(PublishResult::Stored));
    runtime.shut_down();
    runtime.shut_down();
    assert_eq!(runtime.publish(snapshot(2, 1)), Err(OwnerError::ShutDown));
    assert_eq!(runtime.stats().cached_tabs, 0);
}

#[test]
fn close_tab_releases_snapshot_and_reads() {
    let runtime = PageSnapshotRuntime::default();
    runtime.publish(snapshot(1, 1)).unwrap();
    let read = runtime
        .begin_read(&tab(), SessionGeneration::from_raw(1), 1)
        .unwrap();
    assert_eq!(runtime.close_tab(&tab()), Ok(true));
    assert_eq!(runtime.close_tab(&tab()), Ok(false));
    assert_eq!(runtime.next_page(read), Err(OwnerError::Cancelled));
    assert_eq!(runtime.stats().active_reads, 0);
}

#[test]
fn content_reads_are_profile_active_and_generation_fenced() {
    let runtime = PageSnapshotRuntime::default();
    let profile_a = ProfileScope::new("profile-a").unwrap();
    let profile_b = ProfileScope::new("profile-b").unwrap();
    runtime
        .publish_content(
            profile_a.clone(),
            true,
            "selected".to_owned(),
            snapshot(1, 1),
        )
        .unwrap();
    let explicit = AgentTarget::Tab { tab: tab() };
    assert_eq!(
        runtime
            .get_title(&profile_a, &explicit, SessionGeneration::from_raw(1))
            .unwrap()
            .title,
        "Runtime page"
    );
    assert_eq!(
        runtime.get_selection(&profile_b, &explicit, SessionGeneration::from_raw(1)),
        Err(ContentReadRejection::TargetInvalid)
    );
    assert_eq!(runtime.list_targets(&profile_b).unwrap(), Vec::new());
    assert_eq!(runtime.list_targets(&profile_a).unwrap().len(), 1);
    assert_eq!(
        runtime.get_snapshot(&profile_a, &explicit, SessionGeneration::from_raw(0)),
        Err(ContentReadRejection::StaleGeneration)
    );
}

#[test]
fn unscoped_publish_invalidates_agent_metadata() {
    let runtime = PageSnapshotRuntime::default();
    let scope = ProfileScope::new("profile-a").unwrap();
    runtime
        .publish_content(scope.clone(), true, String::new(), snapshot(1, 1))
        .unwrap();
    assert_eq!(runtime.list_targets(&scope).unwrap().len(), 1);
    assert_eq!(runtime.publish(snapshot(1, 2)), Ok(PublishResult::Replaced));
    assert_eq!(runtime.list_targets(&scope).unwrap(), Vec::new());
}

#[test]
fn background_navigation_close_and_shutdown_fail_closed() {
    let runtime = PageSnapshotRuntime::default();
    let scope = ProfileScope::new("profile-a").unwrap();
    runtime
        .publish_content(scope.clone(), true, String::new(), snapshot(1, 1))
        .unwrap();
    let other = PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(
            TabId::new("runtime-tab-2").unwrap(),
            SessionGeneration::from_raw(1),
        ),
        "https://example.test/other".to_owned(),
        "Other page".to_owned(),
        1,
        TruncationInfo::default(),
        vec![ContentBlock::Paragraph {
            text: "Other".to_owned(),
        }],
    )
    .unwrap();
    runtime
        .publish_content(scope.clone(), true, String::new(), other)
        .unwrap();
    let first = AgentTarget::Tab { tab: tab() };
    assert_eq!(
        runtime.get_snapshot(&scope, &first, SessionGeneration::from_raw(1)),
        Err(ContentReadRejection::BackgroundTarget)
    );

    let second_tab = TabId::new("runtime-tab-2").unwrap();
    let second = AgentTarget::Tab {
        tab: second_tab.clone(),
    };
    runtime
        .advance_navigation(second_tab.clone(), SessionGeneration::from_raw(2))
        .unwrap();
    assert_eq!(
        runtime.get_title(&scope, &second, SessionGeneration::from_raw(2)),
        Err(ContentReadRejection::SourceUnavailable)
    );
    let listed = runtime.list_targets(&scope).unwrap();
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].tab_id, tab());
    assert!(!listed[0].active);
    runtime.close_tab(&second_tab).unwrap();
    assert_eq!(
        runtime.get_title(&scope, &second, SessionGeneration::from_raw(2)),
        Err(ContentReadRejection::TargetInvalid)
    );
    runtime.shut_down();
    assert_eq!(runtime.list_targets(&scope).unwrap(), Vec::new());
}
