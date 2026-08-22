use super::*;

#[test]
fn terminating_events_are_closed() {
    assert!(is_session_terminating(LifecycleEvent::SessionEnding));
    assert!(is_session_terminating(LifecycleEvent::Suspending));
    assert!(!is_session_terminating(LifecycleEvent::Resumed));
    assert!(!is_session_terminating(LifecycleEvent::ScreenLocked));
    assert!(!is_session_terminating(LifecycleEvent::ScreenUnlocked));
}

#[test]
fn error_display_golden() {
    assert_eq!(
        LifecycleError::Unavailable.to_string(),
        "lifecycle observation unavailable"
    );
}
