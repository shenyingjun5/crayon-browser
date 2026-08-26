//! M04b lifecycle tests: pure event mappers + listener lifecycle.
//! Real sleep/wake/screen-lock events require a human or QAR harness.

use super::*;

#[test]
fn io_message_mapping_matrix() {
    assert_eq!(
        map_io_message(ffi::K_IO_MESSAGE_SYSTEM_WILL_SLEEP),
        Some(LifecycleEvent::Suspending)
    );
    assert_eq!(
        map_io_message(ffi::K_IO_MESSAGE_SYSTEM_HAS_POWERED_ON),
        Some(LifecycleEvent::Resumed)
    );
    // Unknown IOKit message is ignored.
    assert_eq!(map_io_message(0xDEAD), None);
}

#[test]
fn distributed_name_mapping_matrix() {
    assert_eq!(
        map_distributed_name("com.apple.screenIsLocked"),
        Some(LifecycleEvent::ScreenLocked)
    );
    assert_eq!(
        map_distributed_name("com.apple.screenIsUnlocked"),
        Some(LifecycleEvent::ScreenUnlocked)
    );
    assert_eq!(map_distributed_name("com.apple.other"), None);
}

#[test]
fn listener_registration_roundtrip_does_not_crash() {
    // Real monitor: exercises IOKit registration + run loop thread
    // start/stop.  No events are delivered (no actual sleep/wake).
    let mut monitor = MacLifecycleMonitor::new().expect("monitor");
    PowerLifecycleMonitor::set_listener(&mut monitor, Some(Box::new(|_| {}))).expect("set");
    PowerLifecycleMonitor::set_listener(&mut monitor, None).expect("unset");
    PowerLifecycleMonitor::set_listener(&mut monitor, None).expect("double unset");
    // Drop exercises the cleanup path.
}

#[test]
fn session_ending_not_sourced_on_macos() {
    // Documented platform gap: SessionEnding has no reliable public
    // macOS notification source in v1.  Suspending already terminates
    // live sessions (CP-004), so the safety property is preserved.
    assert_eq!(map_io_message(0xE000_0202), None); // no power-off msg
}
