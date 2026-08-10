//! Link-time smoke proving the pinned Cast-SDK facade is reachable through the
//! adapter without starting discovery, binding ports, or touching the network.

use cast_sender_core::SenderConfig;
use cast_sender_service::SenderCommandService;

#[test]
fn pinned_facade_constructs_without_side_effects() {
    let service = SenderCommandService::new(SenderConfig::default());

    assert!(!service.is_discovery_running());
    assert!(service.list_devices().is_empty());
    assert!(service.current_cast_session().is_none());
}

#[test]
fn facade_types_are_independently_constructible() {
    let config = SenderConfig {
        app_name: "crayon-contract".to_owned(),
        ..SenderConfig::default()
    };

    assert_eq!(config.app_name, "crayon-contract");
    assert!(config.discovery_interface_ips.is_empty());
    assert!(config.local_http_port.is_none());
}
