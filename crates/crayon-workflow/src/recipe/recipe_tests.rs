//! WF-008 coverage: only a verified-success attempt with a fully verified,
//! in-budget trace produces a recipe candidate; every other path rejects
//! deterministically and identity validation is the only error path.

use crate::recipe::{
    generate_candidate, learnable, AttemptOutcome, RecipeCandidate, RecipeGenerationError,
    RecipeRejectReason,
};
use crayon_domain::{
    ActionKind, EffectOutcome, Recipe, SemanticNodeId, TraceStep, WorkflowTrace,
    WORKFLOW_SCHEMA_VERSION,
};

const ORIGIN: &str = "https://example.com";
const NAME: &str = "checkout-helper";

fn node(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("node")
}

fn verified_step(raw: &str, action: ActionKind, summary: &str) -> TraceStep {
    TraceStep {
        node: node(raw),
        action,
        summary: summary.to_owned(),
        outcome: EffectOutcome::Verified,
    }
}

fn success_trace() -> WorkflowTrace {
    WorkflowTrace::new(
        ORIGIN.to_owned(),
        vec![
            verified_step("node1", ActionKind::Click, "open cart"),
            verified_step("node2", ActionKind::Check, "agree terms"),
        ],
    )
    .expect("trace")
}

#[test]
fn verified_success_generates_faithful_candidate() {
    let candidate = generate_candidate(AttemptOutcome::VerifiedSuccess, &success_trace(), NAME, 1)
        .expect("identity");
    let RecipeCandidate::Candidate(recipe) = candidate else {
        panic!("expected candidate");
    };
    assert_eq!(recipe.schema_version, WORKFLOW_SCHEMA_VERSION);
    assert_eq!(recipe.origin, ORIGIN);
    assert_eq!(recipe.name, NAME);
    assert_eq!(recipe.version, 1);
    assert_eq!(recipe.steps.len(), 2);
    assert_eq!(recipe.steps[0].node, node("node1"));
    assert_eq!(recipe.steps[0].action, ActionKind::Click);
    assert_eq!(recipe.steps[0].summary, "open cart");
    assert_eq!(recipe.steps[1].action, ActionKind::Check);
}

#[test]
fn every_non_success_outcome_rejects() {
    for outcome in [
        AttemptOutcome::Failed,
        AttemptOutcome::Cancelled,
        AttemptOutcome::ChallengeIncomplete,
        AttemptOutcome::Indeterminate,
    ] {
        let candidate = generate_candidate(outcome, &success_trace(), NAME, 1).expect("identity");
        assert_eq!(
            candidate,
            RecipeCandidate::Rejected(RecipeRejectReason::NotVerifiedSuccess),
            "outcome {outcome:?} must not learn"
        );
    }
}

#[test]
fn empty_trace_rejects() {
    let empty = WorkflowTrace::new(ORIGIN.to_owned(), Vec::new()).expect("trace");
    let candidate =
        generate_candidate(AttemptOutcome::VerifiedSuccess, &empty, NAME, 1).expect("identity");
    assert_eq!(
        candidate,
        RecipeCandidate::Rejected(RecipeRejectReason::EmptyTrace)
    );
}

#[test]
fn any_unverified_step_rejects_whole_trace() {
    for outcome in [EffectOutcome::Failed, EffectOutcome::Indeterminate] {
        let mut trace = WorkflowTrace::new(
            ORIGIN.to_owned(),
            vec![verified_step("node1", ActionKind::Click, "open cart")],
        )
        .expect("trace");
        trace.steps.push(TraceStep {
            node: node("node2"),
            action: ActionKind::SetText,
            summary: "fill field".to_owned(),
            outcome,
        });
        let candidate =
            generate_candidate(AttemptOutcome::VerifiedSuccess, &trace, NAME, 1).expect("identity");
        assert_eq!(
            candidate,
            RecipeCandidate::Rejected(RecipeRejectReason::UnverifiedStep),
            "trailing {outcome:?} must poison the trace"
        );
    }
}

#[test]
fn full_budget_trace_still_generates_and_over_budget_is_unreachable() {
    // MAX_TRACE_STEPS == MAX_RECIPE_STEPS == 64: a valid trace can never
    // exceed the recipe budget, so the generator's budget branch is pure
    // defense. The boundary (64 steps) must still learn.
    let steps: Vec<TraceStep> = (0..crayon_domain::MAX_RECIPE_STEPS)
        .map(|index| {
            verified_step(
                &format!("node.{index}"),
                ActionKind::Click,
                "bounded summary",
            )
        })
        .collect();
    let trace = WorkflowTrace::new(ORIGIN.to_owned(), steps).expect("trace holds 64");
    let candidate =
        generate_candidate(AttemptOutcome::VerifiedSuccess, &trace, NAME, 1).expect("identity");
    assert!(matches!(candidate, RecipeCandidate::Candidate(_)));
    // The trace type itself refuses a 65th step (type-level guarantee).
    let mut overflow = trace.steps.clone();
    overflow.push(verified_step(
        "node.last",
        ActionKind::Click,
        "one too many",
    ));
    assert!(WorkflowTrace::new(ORIGIN.to_owned(), overflow).is_err());
}

#[test]
fn invalid_identity_is_the_only_error_path() {
    for (name, version) in [
        ("", 1),
        ("Bad Name", 1),
        ("ok-name", 0),
        ("ok-name", 65_536),
    ] {
        assert_eq!(
            generate_candidate(
                AttemptOutcome::VerifiedSuccess,
                &success_trace(),
                name,
                version
            ),
            Err(RecipeGenerationError::InvalidIdentity),
            "name={name:?} version={version} must be Err"
        );
    }
    // Invalid trace origins cannot reach the generator: WorkflowTrace::new
    // rejects them (type-level guarantee), so no extra case exists here.
}

#[test]
fn generated_recipe_is_domain_valid_and_version_bounded() {
    let candidate = generate_candidate(
        AttemptOutcome::VerifiedSuccess,
        &success_trace(),
        "a-1_b",
        65_535,
    )
    .expect("identity");
    let RecipeCandidate::Candidate(recipe) = candidate else {
        panic!("expected candidate");
    };
    // Round-trips through the frozen wire format.
    let encoded = serde_json::to_vec(&recipe).expect("serialize");
    let decoded: Recipe = serde_json::from_slice(&encoded).expect("deserialize");
    assert_eq!(decoded, recipe);
}

#[test]
fn learnable_matches_closed_outcome() {
    assert!(learnable(AttemptOutcome::VerifiedSuccess));
    for outcome in [
        AttemptOutcome::Failed,
        AttemptOutcome::Cancelled,
        AttemptOutcome::ChallengeIncomplete,
        AttemptOutcome::Indeterminate,
    ] {
        assert!(!learnable(outcome));
    }
}
