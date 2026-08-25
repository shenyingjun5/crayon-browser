//! Behaviour tests for the Windows lifecycle monitor (real machine).
//!
//! Real suspend/resume and lock/unlock events need a human at the
//! console; those are CP-W01 device-matrix items.  Here we verify the
//! observation surface constructs, registers, swaps listeners and tears
//! down cleanly without residue.

use super::*;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

#[test]
fn monitor_constructs_and_shuts_down_cleanly() {
    let monitor = WindowsLifecycleMonitor::new().expect("pump starts");
    assert!(std::ptr::addr_of!(monitor) as usize != 0, "sanity");
    drop(monitor); // join must not hang; repeated drops impossible
}

#[test]
fn listener_swap_and_unregister_are_stable() {
    let mut monitor = WindowsLifecycleMonitor::new().expect("pump starts");
    let counter = Arc::new(AtomicU32::new(0));
    let first = Arc::clone(&counter);
    monitor
        .set_listener(Some(Box::new(move |_| {
            first.fetch_add(1, Ordering::SeqCst);
        })))
        .expect("register");
    // Replace then unregister while no real events fire.
    let noop: Box<dyn FnMut(LifecycleEvent) + Send> = Box::new(|_| {});
    monitor.set_listener(Some(noop)).expect("replace");
    monitor.set_listener(None).expect("unregister");
    drop(monitor);
    assert_eq!(counter.load(Ordering::SeqCst), 0);
}
