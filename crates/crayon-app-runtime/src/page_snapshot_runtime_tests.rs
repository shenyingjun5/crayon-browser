use super::*;
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
        let _guard = poison.owner.lock().unwrap();
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
