//! Drift classification (WFL-14).
//!
//! Classifies closed, host-supplied signals into a single drift kind.
//! Multiple simultaneous signals or zero signals classify as `Unknown` —
//! low confidence never masquerades as healthy. `heal_candidate_allowed`
//! is the WFL-15 gate: only low-risk, uniquely-matched, verifiable drift
//! may proceed to a controlled repair.

/// Closed drift signal inputs (all optional facts from trusted layers).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct DriftSignals {
    /// The WFL-02 detector raised challenge evidence after the run.
    pub challenge_detected: bool,
    /// A permission/grant check refused the step.
    pub permission_denied: bool,
    /// The failure was attributable to the network path.
    pub network_failed: bool,
    /// The reported effect did not match the expected step.
    pub effect_mismatch: bool,
    /// The page origin changed since the skill was captured.
    pub origin_changed: bool,
}

/// Closed drift classification.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DriftKind {
    Challenge,
    Permission,
    Network,
    Effect,
    /// The page moved to a different origin.
    Origin,
    /// Signals conflict or none apply: unclassifiable.
    Unknown,
}

/// Risk class of the drifted skill, supplied by the host (closed R0..R4
/// indices). Only R0/R1 drift whose cause is uniquely classified may
/// produce a heal candidate (WFL-15 owns the controlled repair).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DriftRisk {
    Low,
    Elevated,
}

/// Classification result: the kind plus whether a heal candidate may be
/// produced for it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DriftClassification {
    pub kind: DriftKind,
    pub heal_candidate: bool,
}

/// Classifies signals into a single drift kind. Deterministic precedence:
/// unique single-signal inputs classify; every other combination (multiple
/// signals, zero signals) is Unknown and never auto-heals.
#[must_use]
pub fn classify(signals: DriftSignals) -> DriftClassification {
    let set: [bool; 5] = [
        signals.challenge_detected,
        signals.permission_denied,
        signals.network_failed,
        signals.effect_mismatch,
        signals.origin_changed,
    ];
    let active = set.iter().filter(|s| **s).count();
    if active != 1 {
        return DriftClassification {
            kind: DriftKind::Unknown,
            heal_candidate: false,
        };
    }
    let kind = if signals.challenge_detected {
        DriftKind::Challenge
    } else if signals.permission_denied {
        DriftKind::Permission
    } else if signals.network_failed {
        DriftKind::Network
    } else if signals.effect_mismatch {
        DriftKind::Effect
    } else {
        DriftKind::Origin
    };
    DriftClassification {
        kind,
        heal_candidate: false,
    }
}

/// Whether the classification may enter the heal channel: only effect
/// drift on low-risk skills is repair-eligible; challenge, permission,
/// network and origin drift require human handling (WFL-15 gate input).
#[must_use]
pub const fn heal_candidate_allowed(risk: DriftRisk, kind: DriftKind) -> bool {
    matches!((risk, kind), (DriftRisk::Low, DriftKind::Effect))
}

#[cfg(test)]
mod drift_tests;
