use super::*;

#[test]
fn key_validation_matrix() {
    assert!(validate_key("site-skill.token.v1").is_ok());
    for bad in ["", "a b", "ключ", &"k".repeat(65)] {
        assert_eq!(
            validate_key(bad),
            Err(SecureStoreError::InvalidKey),
            "{bad:?}"
        );
    }
    assert_eq!(validate_key(&"k".repeat(64)), Ok(()));
}

#[test]
fn value_validation_matrix() {
    assert!(validate_value(&[]).is_ok());
    assert!(validate_value(&vec![0u8; 4096]).is_ok());
    assert_eq!(
        validate_value(&vec![0u8; 4097]),
        Err(SecureStoreError::ValueTooLarge)
    );
}

#[test]
fn error_display_golden() {
    let cases: &[(SecureStoreError, &str)] = &[
        (SecureStoreError::Unavailable, "secure store is unavailable"),
        (SecureStoreError::AccessDenied, "secure store access denied"),
        (SecureStoreError::NotFound, "secure store entry not found"),
        (SecureStoreError::Corrupted, "secure store entry corrupted"),
        (SecureStoreError::InvalidKey, "secure store key rejected"),
        (
            SecureStoreError::ValueTooLarge,
            "secure store value exceeds size limit",
        ),
    ];
    for (error, expected) in cases {
        assert_eq!(error.to_string(), *expected);
    }
}
