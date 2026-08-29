use super::*;
use crate::{NavigationBinding, TruncationInfo};

fn tab(value: &str) -> TabId {
    TabId::new(value).unwrap()
}

fn snapshot(tab_id: &TabId, generation: u64, revision: u64, blocks: usize) -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(tab_id.clone(), SessionGeneration::from_raw(generation)),
        "https://example.test/article".to_owned(),
        format!("Article {revision}"),
        revision,
        TruncationInfo::default(),
        (0..blocks)
            .map(|index| ContentBlock::Paragraph {
                text: format!("paragraph-{index}"),
            })
            .collect(),
    )
    .unwrap()
}

#[test]
fn ct_007_pagination_is_revision_bound_and_completes() {
    let mut owner = SnapshotOwner::default();
    let tab_id = tab("tab-page");
    assert_eq!(
        owner.publish(snapshot(&tab_id, 3, 7, 513)),
        Ok(PublishResult::Stored)
    );
    let read = owner
        .begin_read(&tab_id, SessionGeneration::from_raw(3), 256)
        .unwrap();
    let first = owner.next_page(read).unwrap();
    assert_eq!(first.blocks().len(), 256);
    assert!(first.has_more());
    assert_eq!(first.revision(), 7);
    assert_eq!(owner.next_page(read).unwrap().blocks().len(), 256);
    let last = owner.next_page(read).unwrap();
    assert_eq!(last.blocks().len(), 1);
    assert!(!last.has_more());
    assert_eq!(owner.next_page(read), Err(OwnerError::ReadComplete));
}

#[test]
fn empty_snapshot_still_returns_one_page() {
    let mut owner = SnapshotOwner::default();
    let tab_id = tab("tab-empty");
    owner.publish(snapshot(&tab_id, 1, 1, 0)).unwrap();
    let read = owner
        .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
        .unwrap();
    let page = owner.next_page(read).unwrap();
    assert!(page.blocks().is_empty());
    assert!(!page.has_more());
}

#[test]
fn ct_002_old_generation_and_revision_never_replace_current() {
    let mut owner = SnapshotOwner::default();
    let tab_id = tab("tab-stale");
    owner.publish(snapshot(&tab_id, 5, 2, 1)).unwrap();
    assert_eq!(
        owner.publish(snapshot(&tab_id, 4, 99, 1)),
        Err(OwnerError::StaleGeneration)
    );
    assert_eq!(
        owner.publish(snapshot(&tab_id, 5, 1, 1)),
        Err(OwnerError::StaleRevision)
    );
    assert_eq!(
        owner.publish(snapshot(&tab_id, 5, 2, 1)),
        Ok(PublishResult::Idempotent)
    );
    let mut conflict = snapshot(&tab_id, 5, 2, 2);
    assert_eq!(
        owner.publish(conflict.clone()),
        Err(OwnerError::RevisionConflict)
    );
    conflict = snapshot(&tab_id, 5, 3, 2);
    assert_eq!(owner.publish(conflict), Ok(PublishResult::Replaced));
    assert_eq!(owner.stats().dropped_stale_results, 2);
}

#[test]
fn navigation_and_new_revision_invalidate_live_reads() {
    let mut owner = SnapshotOwner::default();
    let tab_id = tab("tab-race");
    owner.publish(snapshot(&tab_id, 1, 1, 2)).unwrap();
    let revision_read = owner
        .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
        .unwrap();
    owner.publish(snapshot(&tab_id, 1, 2, 2)).unwrap();
    assert_eq!(
        owner.next_page(revision_read),
        Err(OwnerError::StaleRevision)
    );

    let navigation_read = owner
        .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
        .unwrap();
    assert_eq!(
        owner.advance_navigation(tab_id.clone(), SessionGeneration::from_raw(2)),
        Ok(true)
    );
    assert_eq!(
        owner.next_page(navigation_read),
        Err(OwnerError::StaleGeneration)
    );
    assert_eq!(
        owner.publish(snapshot(&tab_id, 1, 100, 1)),
        Err(OwnerError::StaleGeneration)
    );
    assert_eq!(owner.stats().invalidated_reads, 2);
}

#[test]
fn page_size_cancel_close_and_shutdown_are_explicit() {
    let mut owner = SnapshotOwner::default();
    let tab_id = tab("tab-life");
    owner.publish(snapshot(&tab_id, 1, 1, 2)).unwrap();
    assert_eq!(
        owner.begin_read(&tab_id, SessionGeneration::from_raw(1), 0),
        Err(OwnerError::InvalidPageSize)
    );
    assert_eq!(
        owner.begin_read(&tab_id, SessionGeneration::from_raw(1), MAX_PAGE_BLOCKS + 1),
        Err(OwnerError::InvalidPageSize)
    );
    let read = owner
        .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
        .unwrap();
    assert_eq!(owner.cancel(read), Ok(true));
    assert_eq!(owner.cancel(read), Ok(false));
    assert_eq!(owner.close_tab(&tab_id), Ok(true));
    assert_eq!(owner.close_tab(&tab_id), Ok(false));
    owner.shut_down();
    owner.shut_down();
    assert_eq!(
        owner.advance_navigation(tab_id, SessionGeneration::from_raw(2)),
        Err(OwnerError::ShutDown)
    );
}

#[test]
fn tab_capacity_evicts_only_entries_without_active_reads() {
    let mut owner = SnapshotOwner::default();
    let mut reads = Vec::new();
    for index in 0..MAX_CACHED_TABS {
        let tab_id = tab(&format!("tab-{index}"));
        owner.publish(snapshot(&tab_id, 1, 1, 1)).unwrap();
        reads.push(
            owner
                .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
                .unwrap(),
        );
    }
    let overflow = tab("tab-overflow");
    assert_eq!(
        owner.publish(snapshot(&overflow, 1, 1, 1)),
        Err(OwnerError::CapacityExceeded)
    );
    owner.cancel(reads[0]).unwrap();
    assert_eq!(
        owner.publish(snapshot(&overflow, 1, 1, 1)),
        Ok(PublishResult::Stored)
    );
    assert_eq!(owner.stats().evicted_tabs, 1);
}

#[test]
fn read_and_retired_windows_stay_bounded() {
    let mut owner = SnapshotOwner::default();
    let tab_id = tab("tab-bounds");
    owner.publish(snapshot(&tab_id, 1, 1, 1)).unwrap();
    let mut active = Vec::new();
    for _ in 0..MAX_ACTIVE_READS {
        active.push(
            owner
                .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
                .unwrap(),
        );
    }
    assert_eq!(
        owner.begin_read(&tab_id, SessionGeneration::from_raw(1), 1),
        Err(OwnerError::CapacityExceeded)
    );
    for read in active {
        owner.cancel(read).unwrap();
    }
    for _ in 0..=MAX_RETIRED_READS {
        let read = owner
            .begin_read(&tab_id, SessionGeneration::from_raw(1), 1)
            .unwrap();
        owner.cancel(read).unwrap();
    }
    assert_eq!(owner.stats().retired_reads, MAX_RETIRED_READS);
}
