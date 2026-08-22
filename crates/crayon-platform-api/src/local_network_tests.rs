use super::*;

#[test]
fn interface_name_matrix() {
    assert!(InterfaceName::new("en0").is_ok());
    assert!(InterfaceName::new("Ethernet_2.x").is_ok());
    assert_eq!(
        InterfaceName::new("").unwrap_err(),
        LocalNetworkError::InvalidInterfaceName
    );
    assert_eq!(
        InterfaceName::new("eth 0").unwrap_err(),
        LocalNetworkError::InvalidInterfaceName
    );
    assert_eq!(
        InterfaceName::new(&"i".repeat(65)).unwrap_err(),
        LocalNetworkError::InvalidInterfaceName
    );
    assert!(InterfaceName::new(&"i".repeat(64)).is_ok());
}

#[test]
fn interface_count_bound() {
    assert!(validate_interface_count(0).is_ok());
    assert!(validate_interface_count(64).is_ok());
    assert_eq!(
        validate_interface_count(65).unwrap_err(),
        LocalNetworkError::TooManyInterfaces
    );
}

#[test]
fn error_display_golden() {
    let cases: &[(LocalNetworkError, &str)] = &[
        (
            LocalNetworkError::Unavailable,
            "local network observation unavailable",
        ),
        (
            LocalNetworkError::AccessDenied,
            "local network permission denied",
        ),
        (
            LocalNetworkError::TooManyInterfaces,
            "interface count exceeds limit",
        ),
        (
            LocalNetworkError::InvalidInterfaceName,
            "network interface name rejected",
        ),
    ];
    for (error, expected) in cases {
        assert_eq!(error.to_string(), *expected);
    }
}
