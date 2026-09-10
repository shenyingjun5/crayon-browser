//! HB-010 trust policy: exact-version pinning, revocation, kill switch,
//! capacity bound and deny-by-default.

use crate::api::{ConnectorId, TrustPort};
use crate::trust::{TrustError, TrustRegistry, MAX_TRUST_ENTRIES};

fn id(raw: &str) -> ConnectorId {
    ConnectorId::new(raw).expect("id")
}

#[test]
fn allowed_exact_version_is_trusted() {
    let mut registry = TrustRegistry::new();
    registry.allow(&id("acme.notes"), "1.2.0").expect("allow");
    assert!(registry.is_trusted(&id("acme.notes"), "1.2.0"));
    assert_eq!(registry.verdict(&id("acme.notes"), "1.2.0"), Ok(()));
}

#[test]
fn version_mismatch_and_downgrade_are_rejected() {
    let mut registry = TrustRegistry::new();
    registry.allow(&id("acme.notes"), "1.2.0").unwrap();
    // Tampered re-build / unrelated upgrade.
    assert_eq!(
        registry.verdict(&id("acme.notes"), "1.3.0"),
        Err(crate::trust::UntrustedReason::VersionMismatch)
    );
    // Downgrade below the verified version.
    assert_eq!(
        registry.verdict(&id("acme.notes"), "1.1.0"),
        Err(crate::trust::UntrustedReason::VersionMismatch)
    );
    assert!(!registry.is_trusted(&id("acme.notes"), "1.1.0"));
}

#[test]
fn unknown_connector_denied_by_default() {
    let mut registry = TrustRegistry::new();
    assert_eq!(
        registry.verdict(&id("acme.notes"), "1.0.0"),
        Err(crate::trust::UntrustedReason::UnknownConnector)
    );
    assert!(!registry.is_trusted(&id("acme.notes"), "1.0.0"));
}

#[test]
fn revoke_outranks_allow() {
    let mut registry = TrustRegistry::new();
    registry.allow(&id("acme.notes"), "1.0.0").unwrap();
    registry.revoke(&id("acme.notes"));
    assert!(!registry.is_trusted(&id("acme.notes"), "1.0.0"));
    assert_eq!(
        registry.verdict(&id("acme.notes"), "1.0.0"),
        Err(crate::trust::UntrustedReason::Revoked)
    );
    // Revocation is idempotent.
    registry.revoke(&id("acme.notes"));
    assert!(!registry.is_trusted(&id("acme.notes"), "1.0.0"));
}

#[test]
fn revoke_all_covers_every_connector() {
    let mut registry = TrustRegistry::new();
    registry.allow(&id("acme.notes"), "1.0.0").unwrap();
    registry.allow(&id("acme.mail"), "2.0.0").unwrap();
    assert_eq!(registry.revoke_all(), 2);
    assert!(!registry.is_trusted(&id("acme.notes"), "1.0.0"));
    assert!(!registry.is_trusted(&id("acme.mail"), "2.0.0"));
}

#[test]
fn kill_switch_outranks_everything_and_recovers() {
    let mut registry = TrustRegistry::new();
    registry.allow(&id("acme.notes"), "1.0.0").unwrap();
    registry.set_kill_switch(true);
    assert!(registry.kill_switch_engaged());
    assert_eq!(
        registry.verdict(&id("acme.notes"), "1.0.0"),
        Err(crate::trust::UntrustedReason::KillSwitch)
    );
    assert!(!registry.is_trusted(&id("acme.notes"), "1.0.0"));
    // Allow is refused while engaged; the existing entry survives.
    assert_eq!(
        registry.allow(&id("acme.mail"), "1.0.0"),
        Err(TrustError::KillSwitchEngaged)
    );
    registry.set_kill_switch(false);
    assert!(registry.is_trusted(&id("acme.notes"), "1.0.0"));
    // A connector revoked before the switch stays revoked after it.
    registry.revoke(&id("acme.notes"));
    registry.set_kill_switch(true);
    registry.set_kill_switch(false);
    assert!(!registry.is_trusted(&id("acme.notes"), "1.0.0"));
}

#[test]
fn allow_capacity_is_bounded_fail_closed() {
    let mut registry = TrustRegistry::new();
    let mut count = 0usize;
    for index in 0..MAX_TRUST_ENTRIES + 10 {
        let connector = id(&format!("partner{index}.tool"));
        if registry.allow(&connector, "1.0.0").is_ok() {
            count += 1;
        }
    }
    assert_eq!(count, MAX_TRUST_ENTRIES);
    assert_eq!(registry.allow_count(), MAX_TRUST_ENTRIES);
    // Existing entries keep working after a refused allow.
    assert!(registry.is_trusted(&id("partner0.tool"), "1.0.0"));
}

#[test]
fn empty_version_is_rejected() {
    let mut registry = TrustRegistry::new();
    assert_eq!(
        registry.allow(&id("acme.notes"), ""),
        Err(TrustError::InvalidVersion)
    );
}
