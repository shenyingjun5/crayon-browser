use super::*;

fn err(value: &str, max_len: usize) -> TokenError {
    validate_token(value, max_len).unwrap_err()
}

#[test]
fn accepts_closed_charset_within_limit() {
    assert!(validate_token("profile.v2_backup-01", 64).is_ok());
    assert_eq!(err("", 64), TokenError::Empty);
    assert_eq!(err(&"a".repeat(65), 64), TokenError::TooLong);
    assert_eq!(err("bad key!", 64), TokenError::InvalidCharacter);
    assert_eq!(err("钥匙", 64), TokenError::InvalidCharacter);
}
