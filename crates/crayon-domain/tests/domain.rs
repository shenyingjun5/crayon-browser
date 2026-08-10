use crayon_domain::{ProductIdentity, ProductIdentityError, ProductMode};

#[test]
fn formal_mode_rejects_legacy_adapter_access() {
    assert!(!ProductMode::Formal.permits_legacy_adapter());
    assert!(ProductMode::LegacyDevelopment.permits_legacy_adapter());
}

#[test]
fn identity_rejects_blank_product_name() {
    assert_eq!(
        ProductIdentity::new("  ", ProductMode::Formal),
        Err(ProductIdentityError::EmptyName)
    );
}
