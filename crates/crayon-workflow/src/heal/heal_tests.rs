//! WF-015 coverage: heal gates (channel/risk/uniqueness/challenge/
//! cancellation), retry verification and refusal to human.

use super::{attempt_heal, HealOutcome, HealRefusal, HealRequest, HealStep, MAX_HEAL_ATTEMPTS};
use crate::drift::{DriftKind, DriftRisk};
use crate::runner::RunCancel;
use crate::validation::{FixtureNode, ValidationFixture};
use crayon_domain::{
    ActionKind, CaapError, EffectOutcome, SemanticNodeId, SEMANTIC_MAP_SCHEMA_VERSION,
};

const ORIGIN: &str = "https://example.com";

fn fixture() -> ValidationFixture {
    ValidationFixture::new(
        ORIGIN,
        vec![FixtureNode::new(
            SemanticNodeId::new("node1").unwrap(),
            [ActionKind::Click],
        )],
    )
    .unwrap()
}

fn step() -> HealStep {
    HealStep {
        node: SemanticNodeId::new("node1").unwrap(),
        action: ActionKind::Click,
    }
}

fn good_retry() -> Result<crayon_domain::EffectReport, CaapError> {
    Ok(crayon_domain::EffectReport {
        schema_version: SEMANTIC_MAP_SCHEMA_VERSION,
        tab_id: crayon_domain::TabId::new("tab-1").unwrap(),
        generation: crayon_domain::SessionGeneration::from_raw(1),
        revision: 2,
        action: ActionKind::Click,
        node: SemanticNodeId::new("node1").unwrap(),
        outcome: EffectOutcome::Verified,
        reason: None,
        detail: None,
    })
}

#[test]
fn effect_drift_low_risk_heals_once() {
    let fixture = fixture();
    let step = step();
    let mut retries = 0;
    let outcome = attempt_heal(
        &HealRequest {
            step: &step,
            risk: DriftRisk::Low,
            kind: DriftKind::Effect,
            fixture: &fixture,
            cancel: &RunCancel::new(),
            attempts_used: 0,
            challenge_active: false,
        },
        || {
            retries += 1;
            good_retry()
        },
    )
    .expect("heal");
    assert_eq!(outcome, HealOutcome::Healed);
    assert_eq!(retries, 1);
}

#[test]
fn all_non_effect_kinds_refuse_to_heal() {
    for kind in [
        DriftKind::Challenge,
        DriftKind::Permission,
        DriftKind::Network,
        DriftKind::Origin,
    ] {
        let fixture = fixture();
        let outcome = attempt_heal(
            &HealRequest {
                step: &step(),
                risk: DriftRisk::Low,
                kind,
                fixture: &fixture,
                cancel: &RunCancel::new(),
                attempts_used: 0,
                challenge_active: false,
            },
            good_retry,
        );
        assert_eq!(outcome, Err(HealRefusal::OutsideHealChannel));
    }
}

#[test]
fn elevated_risk_requires_human() {
    let fixture = fixture();
    let outcome = attempt_heal(
        &HealRequest {
            step: &step(),
            risk: DriftRisk::Elevated,
            kind: DriftKind::Effect,
            fixture: &fixture,
            cancel: &RunCancel::new(),
            attempts_used: 0,
            challenge_active: false,
        },
        good_retry,
    );
    assert_eq!(outcome, Err(HealRefusal::RiskTooHigh));
}

#[test]
fn second_heal_attempt_is_unique_refused() {
    let fixture = fixture();
    let outcome = attempt_heal(
        &HealRequest {
            step: &step(),
            risk: DriftRisk::Low,
            kind: DriftKind::Effect,
            fixture: &fixture,
            cancel: &RunCancel::new(),
            attempts_used: MAX_HEAL_ATTEMPTS,
            challenge_active: false,
        },
        good_retry,
    );
    assert_eq!(outcome, Err(HealRefusal::NotUnique));
}

#[test]
fn challenge_and_cancellation_stop_the_heal() {
    let fixture = fixture();
    assert_eq!(
        attempt_heal(
            &HealRequest {
                step: &step(),
                risk: DriftRisk::Low,
                kind: DriftKind::Effect,
                fixture: &fixture,
                cancel: &RunCancel::new(),
                attempts_used: 0,
                challenge_active: true,
            },
            good_retry,
        ),
        Err(HealRefusal::RunStopped)
    );
    let cancel = RunCancel::new();
    cancel.cancel();
    assert_eq!(
        attempt_heal(
            &HealRequest {
                step: &step(),
                risk: DriftRisk::Low,
                kind: DriftKind::Effect,
                fixture: &fixture,
                cancel: &cancel,
                attempts_used: 0,
                challenge_active: false,
            },
            good_retry,
        ),
        Err(HealRefusal::RunStopped)
    );
}

#[test]
fn unverified_retry_fails_to_human() {
    let fixture = fixture();
    let bad_retry = || {
        Ok(crayon_domain::EffectReport {
            schema_version: SEMANTIC_MAP_SCHEMA_VERSION,
            tab_id: crayon_domain::TabId::new("tab-1").unwrap(),
            generation: crayon_domain::SessionGeneration::from_raw(1),
            revision: 2,
            action: ActionKind::Click,
            node: SemanticNodeId::new("node1").unwrap(),
            outcome: EffectOutcome::Failed,
            reason: None,
            detail: None,
        })
    };
    let outcome = attempt_heal(
        &HealRequest {
            step: &step(),
            risk: DriftRisk::Low,
            kind: DriftKind::Effect,
            fixture: &fixture,
            cancel: &RunCancel::new(),
            attempts_used: 0,
            challenge_active: false,
        },
        bad_retry,
    );
    assert_eq!(outcome, Err(HealRefusal::UnverifiedRetry));
}

#[test]
fn unknown_node_in_fixture_refuses() {
    // Fixture that does not know node1.
    let fixture = ValidationFixture::new(
        ORIGIN,
        vec![FixtureNode::new(
            SemanticNodeId::new("node9").unwrap(),
            [ActionKind::Click],
        )],
    )
    .unwrap();
    let outcome = attempt_heal(
        &HealRequest {
            step: &step(),
            risk: DriftRisk::Low,
            kind: DriftKind::Effect,
            fixture: &fixture,
            cancel: &RunCancel::new(),
            attempts_used: 0,
            challenge_active: false,
        },
        good_retry,
    );
    assert_eq!(outcome, Err(HealRefusal::OutsideHealChannel));
}

#[test]
fn max_attempts_constant_is_one() {
    assert_eq!(MAX_HEAL_ATTEMPTS, 1);
}
