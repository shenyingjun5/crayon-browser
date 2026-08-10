//! ManualClock self-tests: deterministic waking, ordering and waiter bound.

use std::time::Duration;
use test_support::clock::{ClockError, ManualClock};

#[tokio::test]
async fn wait_until_resolves_only_after_advance() {
    let clock = ManualClock::new();
    let (tx, mut rx) = tokio::sync::oneshot::channel();
    let task_clock = clock.clone();
    tokio::spawn(async move {
        task_clock
            .wait_until(Duration::from_secs(10))
            .await
            .unwrap();
        let _ = tx.send(());
    });
    // Let the spawned task register its waiter (bounded spin, no sleep).
    for _ in 0..1000 {
        if clock.pending_waiters() == 1 {
            break;
        }
        tokio::task::yield_now().await;
    }
    assert_eq!(clock.pending_waiters(), 1);

    clock.advance(Duration::from_secs(9));
    assert!(
        rx.try_recv().is_err(),
        "deadline not reached: must not wake"
    );

    clock.advance(Duration::from_secs(1));
    rx.await.expect("deadline reached: must wake");
}

#[tokio::test]
async fn wait_until_resolves_immediately_when_deadline_passed() {
    let clock = ManualClock::new();
    clock.advance(Duration::from_secs(5));
    clock
        .wait_until(Duration::from_secs(5))
        .await
        .expect("already-past deadline resolves immediately");
}

#[tokio::test]
async fn waiters_wake_at_their_own_deadlines() {
    let clock = ManualClock::new();
    let (tx, mut rx) = tokio::sync::mpsc::channel(4);
    for deadline_secs in [3u64, 1, 2] {
        let task_clock = clock.clone();
        let task_tx = tx.clone();
        tokio::spawn(async move {
            task_clock
                .wait_until(Duration::from_secs(deadline_secs))
                .await
                .unwrap();
            let _ = task_tx.send(deadline_secs).await;
        });
    }
    for _ in 0..1000 {
        if clock.pending_waiters() == 3 {
            break;
        }
        tokio::task::yield_now().await;
    }
    assert_eq!(clock.pending_waiters(), 3);

    // Each waiter wakes only once its own deadline is reached.
    clock.advance(Duration::from_secs(1));
    let first = tokio::time::timeout(Duration::from_secs(5), rx.recv())
        .await
        .expect("1s waiter must wake")
        .unwrap();
    assert_eq!(first, 1);
    assert_eq!(clock.pending_waiters(), 2);

    clock.advance(Duration::from_secs(1));
    let second = tokio::time::timeout(Duration::from_secs(5), rx.recv())
        .await
        .expect("2s waiter must wake")
        .unwrap();
    assert_eq!(second, 2);

    clock.advance(Duration::from_secs(1));
    let third = tokio::time::timeout(Duration::from_secs(5), rx.recv())
        .await
        .expect("3s waiter must wake")
        .unwrap();
    assert_eq!(third, 3);
    assert_eq!(clock.pending_waiters(), 0);
}

#[tokio::test]
async fn waiter_limit_is_enforced() {
    let clock = ManualClock::new();
    let mut pending = Vec::new();
    for _ in 0..64 {
        let task_clock = clock.clone();
        pending.push(tokio::spawn(async move {
            task_clock.wait_until(Duration::from_secs(3600)).await
        }));
    }
    for _ in 0..10_000 {
        if clock.pending_waiters() == 64 {
            break;
        }
        tokio::task::yield_now().await;
    }
    assert_eq!(clock.pending_waiters(), 64);
    // The 65th waiter fails fast instead of growing an unbounded queue.
    let result = clock.wait_until(Duration::from_secs(3600)).await;
    assert_eq!(result, Err(ClockError::WaiterLimitExceeded));

    clock.advance(Duration::from_secs(3600));
    for task in pending {
        task.await.unwrap().unwrap();
    }
    assert_eq!(clock.pending_waiters(), 0);
}
