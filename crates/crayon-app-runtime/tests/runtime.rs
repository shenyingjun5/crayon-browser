use crayon_app_runtime::RuntimeDescriptor;
use crayon_domain::{ProductIdentityError, ProductMode};
use crayon_ipc_schema::SchemaVersion;

#[test]
fn formal_descriptor_has_consistent_identity_and_handshake() {
    let descriptor = RuntimeDescriptor::formal("Crayon").expect("valid identity");

    assert_eq!(descriptor.identity().mode(), ProductMode::Formal);
    assert_eq!(descriptor.handshake().product_mode(), ProductMode::Formal);
    assert_eq!(
        descriptor.handshake().schema_version(),
        SchemaVersion::CURRENT
    );
}

#[test]
fn formal_descriptor_propagates_identity_validation() {
    assert_eq!(
        RuntimeDescriptor::formal(""),
        Err(ProductIdentityError::EmptyName)
    );
}
