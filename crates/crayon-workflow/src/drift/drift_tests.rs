//! WF-014 coverage: single-signal classification, multi/zero-signal
//! Unknown, heal-channel gating by risk and kind.

use super::{classify, heal_candidate_allowed, DriftKind, DriftRisk, DriftSignals};

#[test]
fn each_single_signal_classifies_uniquely() {
    let base = DriftSignals::default();
    let cases = [
        (
            DriftSignals {
                challenge_detected: true,
                ..base
            },
            DriftKind::Challenge,
        ),
        (
            DriftSignals {
                permission_denied: true,
                ..base
            },
            DriftKind::Permission,
        ),
        (
            DriftSignals {
                network_failed: true,
                ..base
            },
            DriftKind::Network,
        ),
        (
            DriftSignals {
                effect_mismatch: true,
                ..base
            },
            DriftKind::Effect,
        ),
        (
            DriftSignals {
                origin_changed: true,
                ..base
            },
            DriftKind::Origin,
        ),
    ];
    for (signals, kind) in cases {
        let classification = classify(signals);
        assert_eq!(classification.kind, kind);
        // Even a uniquely-classified drift is not auto-healed; WFL-15 owns
        // the controlled-repair gate.
        assert!(!classification.heal_candidate);
    }
}

#[test]
fn zero_signals_is_unknown_not_healthy() {
    let classification = classify(DriftSignals::default());
    assert_eq!(classification.kind, DriftKind::Unknown);
    assert!(!classification.heal_candidate);
}

#[test]
fn multiple_signals_conflict_to_unknown() {
    let conflict = DriftSignals {
        challenge_detected: true,
        permission_denied: true,
        ..DriftSignals::default()
    };
    assert_eq!(classify(conflict).kind, DriftKind::Unknown);

    let conflict = DriftSignals {
        network_failed: true,
        effect_mismatch: true,
        origin_changed: true,
        ..DriftSignals::default()
    };
    assert_eq!(classify(conflict).kind, DriftKind::Unknown);
}

#[test]
fn heal_channel_gated_by_risk_and_kind() {
    // Effect drift on a low-risk skill: heal-eligible.
    assert!(heal_candidate_allowed(DriftRisk::Low, DriftKind::Effect));
    // Everything else: not heal-eligible.
    assert!(!heal_candidate_allowed(
        DriftRisk::Low,
        DriftKind::Challenge
    ));
    assert!(!heal_candidate_allowed(
        DriftRisk::Low,
        DriftKind::Permission
    ));
    assert!(!heal_candidate_allowed(DriftRisk::Low, DriftKind::Network));
    assert!(!heal_candidate_allowed(DriftRisk::Low, DriftKind::Origin));
    assert!(!heal_candidate_allowed(
        DriftRisk::Elevated,
        DriftKind::Effect
    ));
    assert!(!heal_candidate_allowed(
        DriftRisk::Elevated,
        DriftKind::Challenge
    ));
}
