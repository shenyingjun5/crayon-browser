//! Behaviour tests for the Windows network monitor (real machine).

use super::*;
use crayon_platform_api::local_network::MAX_INTERFACES;

#[test]
fn enumeration_reports_valid_bounded_interfaces() {
    let monitor = WindowsNetworkMonitor::new().expect("subscribe");
    let interfaces = monitor.interfaces().expect("enumerate");
    assert!(
        !interfaces.is_empty(),
        "every Windows session has at least a loopback adapter"
    );
    assert!(interfaces.len() <= MAX_INTERFACES);
    for interface in &interfaces {
        // Names are validated newtypes already; assert the loopback flag
        // matches at least one well-known entry.
        let _ = interface.name.as_str();
    }
    assert!(
        interfaces.iter().any(|i| i.is_loopback),
        "loopback adapter must be reported with its capability flag"
    );
}

#[test]
fn repeated_enumeration_is_stable() {
    let monitor = WindowsNetworkMonitor::new().expect("subscribe");
    let first = monitor.interfaces().expect("first");
    let second = monitor.interfaces().expect("second");
    fn names(list: &[NetworkInterface]) -> Vec<&str> {
        let mut names: Vec<&str> = list.iter().map(|i| i.name.as_str()).collect();
        names.sort_unstable();
        names
    }
    assert_eq!(names(&first), names(&second));
}

#[test]
fn listener_registration_roundtrip_does_not_crash() {
    let mut monitor = WindowsNetworkMonitor::new().expect("subscribe");
    monitor
        .set_listener(Some(Box::new(|_event| {})))
        .expect("register");
    monitor.set_listener(None).expect("unregister");
    drop(monitor);
}
