//! Page stream fan-out tests (AGT-06, AG-006/AG-015 契约侧): authorized
//! fan-out, per-client isolation, bounded queues with drop-oldest
//! backpressure and counted gaps, generation fencing, profile-boundary
//! closure, cancel idempotence and bounded instrumentation.

use crate::grant::ProfileScope;
use crate::page_stream::{
    ClientIdError, PageStreamHub, StreamClientId, StreamError, MAX_QUEUED_CHUNKS,
    MAX_STREAM_CLIENTS,
};
use crayon_domain::{SessionGeneration, TabId};
use crayon_page_data::{
    ContentBlock, NavigationBinding, OutputLevel, PageSnapshot, TruncationInfo,
};

fn client(raw: &str) -> StreamClientId {
    StreamClientId::new(raw).expect("valid client id")
}

fn scope(raw: &str) -> ProfileScope {
    ProfileScope::new(raw).expect("valid profile scope")
}

fn snapshot(revision: u64, generation: u64) -> PageSnapshot {
    PageSnapshot::new(
        OutputLevel::Standard,
        NavigationBinding::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(generation),
        ),
        "https://example.com".to_owned(),
        "Title".to_owned(),
        revision,
        TruncationInfo::default(),
        vec![ContentBlock::Heading {
            level: 1,
            text: "Hello".to_owned(),
        }],
    )
    .expect("valid snapshot")
}

#[test]
fn client_ids_are_closed_tokens() {
    assert_eq!(StreamClientId::new(""), Err(ClientIdError::Empty));
    assert_eq!(
        StreamClientId::new("bad id"),
        Err(ClientIdError::InvalidCharset)
    );
    assert_eq!(
        StreamClientId::new(&"a".repeat(65)),
        Err(ClientIdError::TooLong)
    );
    assert!(client("cli-1").as_str() == "cli-1");
}

#[test]
fn fan_out_delivers_only_to_matching_bindings_in_order() {
    let mut hub = PageStreamHub::new();
    hub.subscribe(
        client("cli-a"),
        scope("profile-a"),
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");
    // A second client on another tab must not receive this stream.
    hub.subscribe(
        client("cli-b"),
        scope("profile-a"),
        TabId::new("tab-2").expect("tab id"),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");

    hub.publish(&snapshot(7, 3));
    hub.publish(&snapshot(8, 3));

    let first = hub.next_chunk(&client("cli-a")).expect("chunk");
    assert_eq!(first.seq, 0);
    assert_eq!(first.snapshot.revision(), 7);
    let second = hub.next_chunk(&client("cli-a")).expect("chunk");
    assert_eq!(second.seq, 1);
    assert_eq!(second.snapshot.revision(), 8);
    assert!(hub.next_chunk(&client("cli-a")).is_none());
    // The other-tab client received nothing.
    assert!(hub.next_chunk(&client("cli-b")).is_none());
    let stats = hub.stats();
    assert_eq!((stats.clients, stats.queued, stats.delivered), (2, 0, 2));
}

#[test]
fn queue_overflow_drops_oldest_with_counted_gap() {
    let mut hub = PageStreamHub::new();
    hub.subscribe(
        client("cli-a"),
        scope("profile-a"),
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");
    // Publish past the queue bound; the slow consumer falls behind.
    for revision in 0..(MAX_QUEUED_CHUNKS + 8) as u64 {
        hub.publish(&snapshot(revision, 3));
    }
    let stats = hub.stats();
    assert_eq!(stats.queued, MAX_QUEUED_CHUNKS);
    assert_eq!(stats.dropped, 8);
    // The surviving queue is the newest tail; sequence gaps expose the drop.
    let first = hub.next_chunk(&client("cli-a")).expect("chunk");
    assert_eq!(first.seq, 8, "oldest eight chunks were dropped");
    assert_eq!(first.snapshot.revision(), 8);
    assert_eq!(hub.stats().delivered, 1);
}

#[test]
fn generation_advance_cancels_stale_subscriptions() {
    let mut hub = PageStreamHub::new();
    hub.subscribe(
        client("cli-a"),
        scope("profile-a"),
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");
    hub.subscribe(
        client("cli-b"),
        scope("profile-a"),
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(5),
    )
    .expect("subscribe");
    // Advancing to generation 5 cancels only the older subscription.
    assert_eq!(
        hub.advance_generation(
            &TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(5)
        ),
        1
    );
    hub.publish(&snapshot(9, 5));
    // cli-a is gone; cli-b still streams.
    assert!(hub.next_chunk(&client("cli-a")).is_none());
    assert!(hub.next_chunk(&client("cli-b")).is_some());
    assert_eq!(hub.stats().cancelled_by_generation, 1);
    // Same-generation re-read does not cancel anything.
    assert_eq!(
        hub.advance_generation(
            &TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(5)
        ),
        0
    );
}

#[test]
fn profile_boundary_closes_without_content_leak() {
    let mut hub = PageStreamHub::new();
    hub.subscribe(
        client("cli-a"),
        scope("profile-a"),
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");
    hub.publish(&snapshot(7, 3));
    // Closing profile-a drops queued content with the subscription.
    assert_eq!(hub.close_profile(&scope("profile-a")), 1);
    assert!(hub.next_chunk(&client("cli-a")).is_none());
    assert_eq!(hub.stats().queued, 0);
}

#[test]
fn subscribe_rejections_and_cancel_idempotence() {
    let mut hub = PageStreamHub::new();
    let tab = TabId::new("tab-1").expect("tab id");
    hub.subscribe(
        client("cli-a"),
        scope("profile-a"),
        tab.clone(),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");
    assert_eq!(
        hub.subscribe(
            client("cli-a"),
            scope("profile-a"),
            tab.clone(),
            SessionGeneration::from_raw(3)
        ),
        Err(StreamError::DuplicateClient)
    );
    // Capacity bound: fill to MAX_STREAM_CLIENTS then reject.
    for index in 1..MAX_STREAM_CLIENTS {
        hub.subscribe(
            client(&format!("cli-{index}")),
            scope("profile-a"),
            tab.clone(),
            SessionGeneration::from_raw(3),
        )
        .expect("subscribe");
    }
    assert_eq!(
        hub.subscribe(
            client("cli-overflow"),
            scope("profile-a"),
            tab,
            SessionGeneration::from_raw(3)
        ),
        Err(StreamError::CapacityExceeded)
    );
    assert_eq!(hub.stats().clients, MAX_STREAM_CLIENTS);
    // Cancel is idempotent; unknown clients report no removal.
    assert!(hub.cancel(&client("cli-a")));
    assert!(!hub.cancel(&client("cli-a")));
    assert!(
        hub.next_chunk(&client("cli-a")).is_none(),
        "cancelled client has no queue"
    );
}

#[test]
fn shutdown_is_idempotent_and_rejects_new_subscriptions() {
    let mut hub = PageStreamHub::new();
    hub.subscribe(
        client("cli-a"),
        scope("profile-a"),
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
    )
    .expect("subscribe");
    assert_eq!(hub.shut_down(), 1);
    assert_eq!(hub.shut_down(), 0);
    hub.publish(&snapshot(7, 3));
    assert_eq!(hub.stats(), crate::page_stream::StreamStats::default());
    assert_eq!(
        hub.subscribe(
            client("cli-b"),
            scope("profile-a"),
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(3)
        ),
        Err(StreamError::ShutDown)
    );
}
