//! Deterministic behaviour tests for the bounded event relay.

use super::*;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{mpsc, Arc};

#[test]
fn delivers_in_order_to_registered_listener() {
    let relay: EventRelay<u32> = EventRelay::start();
    let (tx, rx) = mpsc::channel();
    relay.set_listener(Some(Box::new(move |e| {
        let _ = tx.send(e);
    })));
    let sink = relay.sink();
    for e in 0..8 {
        sink.push(e);
    }
    for expected in 0..8 {
        assert_eq!(
            rx.recv_timeout(std::time::Duration::from_secs(2)),
            Ok(expected)
        );
    }
}

#[test]
fn overflow_sheds_oldest_and_counts_drops() {
    // No listener: the queue fills to capacity; further pushes shed.
    let relay: EventRelay<u32> = EventRelay::start();
    let sink = relay.sink();
    for e in 0..(RELAY_CAPACITY as u32 + 10) {
        sink.push(e);
    }
    let (tx, rx) = mpsc::channel();
    relay.set_listener(Some(Box::new(move |e| {
        let _ = tx.send(e);
    })));
    // First delivered event is the oldest survivor, not element 0.
    let first = rx
        .recv_timeout(std::time::Duration::from_secs(2))
        .expect("event");
    assert!(
        first >= 10,
        "oldest events must have been shed, got {first}"
    );
    let mut received = 1;
    while rx
        .recv_timeout(std::time::Duration::from_millis(200))
        .is_ok()
    {
        received += 1;
    }
    assert_eq!(
        received, RELAY_CAPACITY,
        "exactly capacity events survive a burst"
    );
}

#[test]
fn listener_replacement_takes_effect() {
    let relay: EventRelay<u32> = EventRelay::start();
    let counter = Arc::new(AtomicU32::new(0));
    let c1 = Arc::clone(&counter);
    relay.set_listener(Some(Box::new(move |_| {
        c1.fetch_add(1, Ordering::SeqCst);
    })));
    let c2 = Arc::clone(&counter);
    relay.set_listener(Some(Box::new(move |e| {
        c2.fetch_add(e, Ordering::SeqCst);
    })));
    relay.sink().push(7);
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(2);
    while counter.load(Ordering::SeqCst) < 7 && std::time::Instant::now() < deadline {
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
    assert_eq!(
        counter.load(Ordering::SeqCst),
        7,
        "old listener must not fire"
    );
}

#[test]
fn unregister_stops_delivery_and_close_is_idempotent() {
    let relay: EventRelay<u32> = EventRelay::start();
    let counter = Arc::new(AtomicU32::new(0));
    let c = Arc::clone(&counter);
    relay.set_listener(Some(Box::new(move |_| {
        c.fetch_add(1, Ordering::SeqCst);
    })));
    relay.set_listener(None);
    relay.sink().push(1);
    relay.close();
    relay.close(); // idempotent
    relay.sink().push(2); // late push after close is dropped
    std::thread::sleep(std::time::Duration::from_millis(100));
    assert_eq!(counter.load(Ordering::SeqCst), 0);
}
