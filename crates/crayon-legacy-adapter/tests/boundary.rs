use crayon_domain::ProductMode;
use crayon_legacy_adapter::{LegacyBoundary, LegacyBoundaryError};

#[test]
fn formal_product_cannot_enter_legacy_boundary() {
    assert_eq!(
        LegacyBoundary::enter(ProductMode::Formal),
        Err(LegacyBoundaryError::FormalProductForbidden)
    );
}

#[test]
fn explicit_legacy_mode_can_enter_boundary() {
    let boundary = LegacyBoundary::enter(ProductMode::LegacyDevelopment)
        .expect("explicit legacy mode is accepted");

    assert_eq!(boundary.mode(), ProductMode::LegacyDevelopment);
}
