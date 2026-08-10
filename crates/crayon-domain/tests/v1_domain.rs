//! v1 domain contract: strong id validation, session generation ordering,
//! capability roundtrip, and stable product-mode wire names (FND-08).

use crayon_domain::{
    BrowserEngineKind, CoreError, IdError, LocalDiscoveryKind, PlatformCapabilities,
    ProtectedSurfaceKind, ReceiverCapabilities, ResourceId, SecureStoreKind, SessionGeneration,
    SessionId, TabId,
};

#[test]
fn strong_ids_validate_boundaries() {
    assert_eq!(TabId::new(""), Err(IdError::Empty));
    assert_eq!(SessionId::new(&"a".repeat(129)), Err(IdError::TooLong));
    assert_eq!(ResourceId::new("has space"), Err(IdError::InvalidCharset));
    assert_eq!(ResourceId::new("https://x"), Err(IdError::InvalidCharset));

    let id = TabId::new("tab-01_AB").unwrap();
    assert_eq!(id.as_str(), "tab-01_AB");
    // Wire form is the plain validated string.
    assert_eq!(serde_json::to_string(&id).unwrap(), "\"tab-01_AB\"");
    assert_eq!(serde_json::from_str::<TabId>("\"tab-01_AB\"").unwrap(), id);
    assert!(serde_json::from_str::<TabId>("\"bad id\"").is_err());
}

#[test]
fn session_generation_orders_and_never_wraps() {
    let initial = SessionGeneration::INITIAL;
    let next = initial.advance().unwrap();
    assert!(next.supersedes(initial));
    assert!(!initial.supersedes(next));
    assert!(!initial.supersedes(initial));
    assert_eq!(SessionGeneration::from_raw(u64::MAX).advance(), None);
}

#[test]
fn capabilities_roundtrip_the_design_example() {
    // Technical design §4.1 example, byte-level wire keys.
    let caps = PlatformCapabilities::new(
        BrowserEngineKind::Cef,
        true,
        true,
        true,
        LocalDiscoveryKind::MdnsUdp,
        SecureStoreKind::OsNative,
        ProtectedSurfaceKind::Blocked,
    );
    let json = serde_json::to_value(caps).unwrap();
    assert_eq!(
        json,
        serde_json::json!({
            "browser_engine": "cef",
            "tab_video": true,
            "system_audio": true,
            "hardware_h264": true,
            "local_discovery": "mdns+udp",
            "secure_store": "os_native",
            "protected_surface": "blocked",
        })
    );
    assert_eq!(
        serde_json::from_value::<PlatformCapabilities>(json).unwrap(),
        caps
    );

    let receiver = ReceiverCapabilities::new(true, true, true, true, false, false, 2160);
    let json = serde_json::to_value(receiver).unwrap();
    assert_eq!(json["max_height"], 2160);
    assert_eq!(
        serde_json::from_value::<ReceiverCapabilities>(json).unwrap(),
        receiver
    );
}

#[test]
fn core_error_codes_match_wire_strings() {
    assert_eq!(CoreError::DrmProtected.code(), "drm_protected");
    assert_eq!(
        CoreError::from_code("drm_protected"),
        Some(CoreError::DrmProtected)
    );
    assert_eq!(CoreError::from_code("unknown"), None);
}

#[test]
fn product_mode_wire_names_are_stable() {
    assert_eq!(
        serde_json::to_string(&crayon_domain::ProductMode::Formal).unwrap(),
        "\"formal\""
    );
    assert_eq!(
        serde_json::to_string(&crayon_domain::ProductMode::LegacyDevelopment).unwrap(),
        "\"legacy_development\""
    );
}
