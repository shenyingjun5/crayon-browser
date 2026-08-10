use crayon_domain::ProductMode;
use crayon_ipc_schema::{Handshake, SchemaVersion};
use std::num::NonZeroU16;

#[test]
fn current_version_accepts_only_the_same_version() {
    let next = SchemaVersion::new(NonZeroU16::new(2).expect("non-zero constant"));

    assert!(SchemaVersion::CURRENT.is_supported_by(SchemaVersion::CURRENT));
    assert!(!SchemaVersion::CURRENT.is_supported_by(next));
}

#[test]
fn handshake_preserves_the_explicit_product_mode() {
    let handshake = Handshake::current(ProductMode::Formal);

    assert_eq!(handshake.product_mode(), ProductMode::Formal);
    assert_eq!(handshake.schema_version(), SchemaVersion::CURRENT);
}
