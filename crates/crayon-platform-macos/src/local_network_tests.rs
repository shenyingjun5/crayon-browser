//! M04b local-network tests: real enumeration + route message mapping
//! + listener lifecycle.

use super::*;
use crayon_platform_api::local_network::LocalNetworkMonitor;

#[test]
fn enumeration_reports_valid_bounded_interfaces() {
    let monitor = MacNetworkMonitor::new().expect("monitor");
    let interfaces = LocalNetworkMonitor::interfaces(&monitor).expect("enumerate");
    assert!(!interfaces.is_empty(), "at least loopback must exist");
    assert!(interfaces.len() <= MAX_INTERFACES);
    for iface in &interfaces {
        // Names are validated newtypes already; assert the loopback
        // flag on at least one interface (lo0 always exists).
        let _ = iface.is_loopback;
        let _ = iface.is_up;
    }
    assert!(
        interfaces.iter().any(|i| i.is_loopback && i.is_up),
        "loopback interface must be up"
    );
}

#[test]
fn repeated_enumeration_is_stable() {
    let monitor = MacNetworkMonitor::new().expect("monitor");
    let first = LocalNetworkMonitor::interfaces(&monitor).expect("first");
    let second = LocalNetworkMonitor::interfaces(&monitor).expect("second");
    fn names(list: &[NetworkInterface]) -> Vec<&str> {
        list.iter().map(|i| i.name.as_str()).collect()
    }
    assert_eq!(names(&first), names(&second));
}

#[test]
fn listener_registration_roundtrip_does_not_crash() {
    let mut monitor = MacNetworkMonitor::new().expect("monitor");
    LocalNetworkMonitor::set_listener(&mut monitor, Some(Box::new(|_| {}))).expect("set");
    LocalNetworkMonitor::set_listener(&mut monitor, None).expect("unset");
    LocalNetworkMonitor::set_listener(&mut monitor, None).expect("double unset");
}

#[test]
fn route_message_mapping_matrix() {
    use std::mem::size_of;

    // Build a synthetic RTM_IFINFO message with IFF_UP set.
    let mut msg = vec![0u8; size_of::<ffi::RtMsghdr>() + size_of::<ffi::IfMsghdr>()];
    let header = ffi::RtMsghdr {
        rtm_msglen: msg.len() as u16,
        rtm_version: 3,
        rtm_type: ffi::RTM_IFINFO,
        rtm_index: 1,
        rtm_flags: 0,
        rtm_addrs: 0,
        rtm_pid: 0,
        rtm_seq: 0,
        rtm_errno: 0,
        rtm_use: 0,
    };
    let ifm = ffi::IfMsghdr {
        ifm_addrs: 0,
        ifm_flags: ffi::IFF_UP as i32,
        ifm_index: 1,
        _pad: [0; 2],
    };
    // SAFETY: both pointers are valid for the copy length.
    msg[..size_of::<ffi::RtMsghdr>()].copy_from_slice(unsafe {
        std::slice::from_raw_parts(&header as *const _ as *const u8, size_of::<ffi::RtMsghdr>())
    });
    // SAFETY: both pointers are valid for the copy length.
    msg[size_of::<ffi::RtMsghdr>()..].copy_from_slice(unsafe {
        std::slice::from_raw_parts(&ifm as *const _ as *const u8, size_of::<ffi::IfMsghdr>())
    });
    // SAFETY: msg is at least RtMsghdr-sized and aligned to 1.
    let header = unsafe { std::ptr::read_unaligned(msg.as_ptr().cast::<ffi::RtMsghdr>()) };
    let event = MacNetworkMonitor::map_message(&header, &msg).expect("IFINFO event");
    match event {
        NetworkChangeEvent::InterfaceUp(name) => assert_eq!(name.as_str(), "if1"),
        _ => panic!("expected InterfaceUp"),
    }

    // RTM_ADD with no destination = default route change.
    let mut add_msg = vec![0u8; size_of::<ffi::RtMsghdr>() + 8];
    let add_header = ffi::RtMsghdr {
        rtm_msglen: add_msg.len() as u16,
        rtm_version: 3,
        rtm_type: ffi::RTM_ADD,
        rtm_index: 0,
        rtm_flags: 0,
        rtm_addrs: 0, // no RTA_DST
        rtm_pid: 0,
        rtm_seq: 0,
        rtm_errno: 0,
        rtm_use: 0,
    };
    // SAFETY: both pointers are valid for the copy length.
    add_msg[..size_of::<ffi::RtMsghdr>()].copy_from_slice(unsafe {
        std::slice::from_raw_parts(
            &add_header as *const _ as *const u8,
            size_of::<ffi::RtMsghdr>(),
        )
    });
    // SAFETY: add_msg is at least RtMsghdr-sized and aligned to 1.
    let add_header = unsafe { std::ptr::read_unaligned(add_msg.as_ptr().cast::<ffi::RtMsghdr>()) };
    let event = MacNetworkMonitor::map_message(&add_header, &add_msg).expect("default route");
    assert_eq!(event, NetworkChangeEvent::DefaultRouteChanged);

    // Unknown message type is ignored.
    let unknown_header = ffi::RtMsghdr {
        rtm_type: 0xFF,
        ..add_header
    };
    assert!(MacNetworkMonitor::map_message(&unknown_header, &add_msg).is_none());
}
