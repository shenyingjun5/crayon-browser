//! HB-011 coverage: tenant isolation in the token vault, CSRF state
//! verify, PKCE S256 roundtrip with an injected digest, exact-match
//! redirect policy and minimal-scope enforcement.

use super::{
    base64url_no_pad, validate_redirect, validate_scopes, OAuthError, OAuthState, PkceChallenge,
    TokenVault,
};
use crate::api::{ConnectorDescriptor, ConnectorId};
use crayon_platform_api::secure_store::{SecureStore, SecureStoreError};
use std::collections::BTreeMap;

struct MemoryStore {
    data: BTreeMap<String, Vec<u8>>,
}

impl MemoryStore {
    fn new() -> Self {
        Self {
            data: BTreeMap::new(),
        }
    }
}

impl SecureStore for MemoryStore {
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        self.data.insert(key.to_owned(), value.to_vec());
        Ok(())
    }

    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        Ok(self.data.get(key).cloned())
    }

    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        self.data.remove(key);
        Ok(())
    }
}

fn connector(raw: &str) -> ConnectorId {
    ConnectorId::new(raw).expect("id")
}

fn descriptor() -> ConnectorDescriptor {
    ConnectorDescriptor::new(
        connector("acme.notes"),
        "1.0.0",
        &["notes.read", "notes.write"],
    )
    .expect("descriptor")
}

#[test]
fn tenants_cannot_cross_read_tokens() {
    let mut vault = TokenVault::new(MemoryStore::new());
    let acme = connector("acme.notes");
    vault.store_token(&acme, "alice", b"alice-token").unwrap();
    // Same connector, different tenant: no token.
    assert!(vault.token_handle(&acme, "bob").is_none());
    // Different connector, same tenant: no token.
    assert!(vault
        .token_handle(&connector("acme.mail"), "alice")
        .is_none());
    // Handles are stable and monotonic per key.
    let alice = vault.token_handle(&acme, "alice").unwrap();
    vault.store_token(&acme, "bob", b"bob-token").unwrap();
    let bob = vault.token_handle(&acme, "bob").unwrap();
    assert_ne!(alice, bob);
    assert_eq!(vault.token_handle(&acme, "alice"), Some(alice));
}

#[test]
fn clear_connector_removes_all_accounts_only() {
    let mut vault = TokenVault::new(MemoryStore::new());
    let acme = connector("acme.notes");
    let mail = connector("acme.mail");
    vault.store_token(&acme, "alice", b"a").unwrap();
    vault.store_token(&acme, "bob", b"b").unwrap();
    vault.store_token(&mail, "alice", b"c").unwrap();
    assert_eq!(vault.clear_connector(&acme).unwrap(), 2);
    assert!(vault.token_handle(&acme, "alice").is_none());
    assert!(vault.token_handle(&acme, "bob").is_none());
    assert!(vault.token_handle(&mail, "alice").is_some());
    // Idempotent.
    assert_eq!(vault.clear_connector(&acme).unwrap(), 0);
}

#[test]
fn state_generation_and_constant_time_verify() {
    let mut entropy = 0u8;
    let state = OAuthState::generate(|| {
        entropy = entropy.wrapping_add(7);
        entropy
    });
    assert_eq!(state.len(), 64);
    assert!(state.bytes().all(|b| b.is_ascii_hexdigit()));
    assert!(OAuthState::verify(&state, &state));
    let flip = if state.starts_with('0') { '1' } else { '0' };
    let mut tampered = state.clone();
    tampered.replace_range(0..1, &flip.to_string());
    assert!(!OAuthState::verify(&state, &tampered));
    assert!(!OAuthState::verify(&state, ""));
}

#[test]
fn pkce_roundtrip_and_verifier_charset() {
    struct FixedSha;
    impl super::Sha256Port for FixedSha {
        fn sha256(&mut self, data: &[u8]) -> [u8; 32] {
            // Deterministic non-crypto digest for the policy test: the
            // real SHA-256 is injected by the host.
            let mut out = [0u8; 32];
            for (index, byte) in data.iter().enumerate() {
                out[index % 32] ^= byte.wrapping_add(index as u8);
            }
            out
        }
    }
    let verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"; // RFC 7636 shape
    let challenge = PkceChallenge::s256(verifier, &mut FixedSha).unwrap();
    // Base64url without padding.
    assert!(!challenge.contains('+') && !challenge.contains('/') && !challenge.contains('='));
    assert!(PkceChallenge::verify(verifier, &challenge, &mut FixedSha).unwrap());
    assert!(!PkceChallenge::verify(verifier, "wrong-challenge", &mut FixedSha).unwrap());

    // Verifier grammar: too short, bad charset.
    assert!(!PkceChallenge::valid_verifier("short"));
    assert!(!PkceChallenge::valid_verifier(&"a".repeat(129)));
    assert!(PkceChallenge::valid_verifier(&"a".repeat(43)));
    assert!(PkceChallenge::valid_verifier(&"a".repeat(128)));
    assert!(PkceChallenge::s256("bad verifier with spaces", &mut FixedSha).is_err());
}

#[test]
fn base64url_encoding_matches_rfc4648_vectors() {
    assert_eq!(base64url_no_pad(&[]), "");
    assert_eq!(base64url_no_pad(&[0xf5]), "9Q"); // 111101 01
    assert_eq!(base64url_no_pad(&[0xf5, 0x57]), "9Vc");
    assert_eq!(base64url_no_pad(&[0xf5, 0x57, 0xbc]), "9Ve8");
}

#[test]
fn redirect_requires_exact_registered_entry() {
    let allowed = ["https://partner.example/callback"];
    assert_eq!(
        validate_redirect("https://partner.example/callback", &allowed),
        Ok(())
    );
    assert_eq!(
        validate_redirect("https://partner.example/callback/extra", &allowed),
        Err(OAuthError::RedirectUnknown)
    );
    assert_eq!(
        validate_redirect("", &allowed),
        Err(OAuthError::RedirectUnknown)
    );
}

#[test]
fn scopes_must_stay_within_descriptor() {
    let descriptor = descriptor();
    assert_eq!(validate_scopes(&["notes.read"], &descriptor), Ok(()));
    assert_eq!(
        validate_scopes(&["notes.read", "notes.write"], &descriptor),
        Ok(())
    );
    assert!(validate_scopes(&["notes.admin"], &descriptor).is_err());
    assert!(validate_scopes(&[], &descriptor).is_err());
}
