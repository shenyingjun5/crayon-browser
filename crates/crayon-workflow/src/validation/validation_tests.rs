//! WF-011 coverage: fixture/sandbox matching, skill validation matrix,
//! effect single-judgment and whole-run 1:1 line-up. Pure functions only.

use super::{
    validate_effect, validate_run, validate_skill, ExpectedEffect, FixtureNode, ReportedEffect,
    ValidationFixture,
};
use crate::recipe::{generate_candidate, AttemptOutcome};
use crate::store::SkillStore;
use crayon_domain::{
    ActionKind, EffectOutcome, SemanticNodeId, SkillStatus, TraceStep, WorkflowTrace,
};
use crayon_platform_api::secure_store::{SecureStore, SecureStoreError};
use std::collections::BTreeMap;

const ORIGIN: &str = "https://example.com";

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

fn node(id: &str, actions: &[ActionKind]) -> FixtureNode {
    FixtureNode::new(SemanticNodeId::new(id).unwrap(), actions.iter().copied())
}

fn fixture() -> ValidationFixture {
    ValidationFixture::new(
        ORIGIN,
        vec![
            node("node1", &[ActionKind::Click]),
            node("node2", &[ActionKind::SetText, ActionKind::Clear]),
        ],
    )
    .expect("fixture")
}

fn saved_skill() -> crayon_domain::SiteSkill {
    let mut store = SkillStore::new(MemoryStore::new());
    let trace = WorkflowTrace::new(
        ORIGIN.to_owned(),
        vec![
            TraceStep {
                node: SemanticNodeId::new("node1").unwrap(),
                action: ActionKind::Click,
                summary: "open form".to_owned(),
                outcome: EffectOutcome::Verified,
            },
            TraceStep {
                node: SemanticNodeId::new("node2").unwrap(),
                action: ActionKind::SetText,
                summary: "fill title".to_owned(),
                outcome: EffectOutcome::Verified,
            },
        ],
    )
    .unwrap();
    let candidate =
        generate_candidate(AttemptOutcome::VerifiedSuccess, &trace, "form-helper", 1).unwrap();
    let crate::recipe::RecipeCandidate::Candidate(recipe) = candidate else {
        panic!("expected candidate");
    };
    store.save_candidate(recipe).unwrap()
}

fn reported(step: usize, action: ActionKind, outcome: EffectOutcome) -> ReportedEffect {
    ReportedEffect {
        node: SemanticNodeId::new(&format!("node{step}")).unwrap(),
        action,
        outcome,
    }
}

#[test]
fn valid_skill_passes_against_matching_fixture() {
    let skill = saved_skill();
    assert_eq!(validate_skill(&skill, &fixture()), Ok(()));
}

#[test]
fn origin_mismatch_is_rejected() {
    let skill = saved_skill();
    let other = ValidationFixture::new(
        "https://other.example",
        vec![
            node("node1", &[ActionKind::Click]),
            node("node2", &[ActionKind::SetText]),
        ],
    )
    .unwrap();
    assert_eq!(
        validate_skill(&skill, &other),
        Err(super::ValidationError::OriginMismatch)
    );
    // Invalid fixture origins fail closed as a mismatch.
    assert!(ValidationFixture::new("not-an-origin", []).is_err());
}

#[test]
fn unknown_node_and_unsupported_action_are_rejected() {
    let skill = saved_skill();
    let unknown_node = ValidationFixture::new(
        ORIGIN,
        vec![node("node9", &[ActionKind::Click, ActionKind::SetText])],
    )
    .unwrap();
    assert_eq!(
        validate_skill(&skill, &unknown_node),
        Err(super::ValidationError::NodeUnknown)
    );

    // node1 exists but the fixture says it only supports Clear.
    let unsupported = ValidationFixture::new(
        ORIGIN,
        vec![
            node("node1", &[ActionKind::Clear]),
            node("node2", &[ActionKind::SetText]),
        ],
    )
    .unwrap();
    assert_eq!(
        validate_skill(&skill, &unsupported),
        Err(super::ValidationError::ActionUnsupported)
    );
}

#[test]
fn empty_and_oversized_skills_are_rejected() {
    let fixture = fixture();
    // The generator never emits an empty recipe (WFL-08 gate); build the
    // empty skill directly from the domain type.
    let empty_recipe = crayon_domain::Recipe::new(ORIGIN.to_owned(), "empty", 1, vec![]).unwrap();
    let empty_skill =
        crayon_domain::SiteSkill::new(empty_recipe, SkillStatus::Candidate, 1).unwrap();
    assert_eq!(
        validate_skill(&empty_skill, &fixture),
        Err(super::ValidationError::EmptySkill)
    );

    // Budget parity: the validation budget equals the domain recipe
    // budget, so >64-step skills cannot exist (domain constructor
    // refuses); the validator's own budget branch is pure defense.
    assert_eq!(super::MAX_VALIDATED_STEPS, crayon_domain::MAX_RECIPE_STEPS);
    // Boundary: a 64-step skill validates (all steps known to fixture).
    let known_steps: Vec<crayon_domain::RecipeStep> = (0..64)
        .map(|index| crayon_domain::RecipeStep {
            node: SemanticNodeId::new("node1").unwrap(),
            action: ActionKind::Click,
            summary: format!("step {index}"),
        })
        .collect();
    let big_recipe = crayon_domain::Recipe::new(ORIGIN.to_owned(), "big", 1, known_steps).unwrap();
    let big_skill = crayon_domain::SiteSkill::new(big_recipe, SkillStatus::Candidate, 1).unwrap();
    assert_eq!(validate_skill(&big_skill, &fixture), Ok(()));
}

#[test]
fn effect_single_judgment_matches_node_action_and_outcome() {
    let fixture = fixture();
    let expected = ExpectedEffect {
        node: SemanticNodeId::new("node1").unwrap(),
        action: ActionKind::Click,
    };
    assert_eq!(
        validate_effect(
            &expected,
            &reported(1, ActionKind::Click, EffectOutcome::Verified),
            &fixture
        ),
        Ok(())
    );
    // Node swap.
    assert!(validate_effect(
        &expected,
        &reported(2, ActionKind::Click, EffectOutcome::Verified),
        &fixture
    )
    .is_err());
    // Action swap.
    assert!(validate_effect(
        &expected,
        &reported(1, ActionKind::SetText, EffectOutcome::Verified),
        &fixture
    )
    .is_err());
    // Unverified outcomes never pass.
    for outcome in [EffectOutcome::Failed, EffectOutcome::Indeterminate] {
        assert!(validate_effect(
            &expected,
            &reported(1, ActionKind::Click, outcome),
            &fixture
        )
        .is_err());
    }
}

#[test]
fn run_validation_requires_exact_one_to_one_lineup() {
    let skill = saved_skill();
    // Exact 1:1.
    assert_eq!(
        validate_run(
            &skill,
            &[
                reported(1, ActionKind::Click, EffectOutcome::Verified),
                reported(2, ActionKind::SetText, EffectOutcome::Verified),
            ],
            &fixture()
        ),
        Ok(())
    );
    // Missing second effect.
    assert!(validate_run(
        &skill,
        &[reported(1, ActionKind::Click, EffectOutcome::Verified)],
        &fixture()
    )
    .is_err());
    // Extra effect.
    assert!(validate_run(
        &skill,
        &[
            reported(1, ActionKind::Click, EffectOutcome::Verified),
            reported(2, ActionKind::SetText, EffectOutcome::Verified),
            reported(1, ActionKind::Click, EffectOutcome::Verified),
        ],
        &fixture()
    )
    .is_err());
    // Order matters (first effect must match the first step).
    assert!(validate_run(
        &skill,
        &[
            reported(2, ActionKind::SetText, EffectOutcome::Verified),
            reported(1, ActionKind::Click, EffectOutcome::Verified),
        ],
        &fixture()
    )
    .is_err());
}
