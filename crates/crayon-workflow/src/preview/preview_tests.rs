//! WF-009 coverage: preview disclosure, explicit confirmation, expiry,
//! candidate-change invalidation and idempotent terminals. No assertion
//! carries parameter values or secrets.

use super::{PreviewError, PreviewPhase, SkillPreviewController};
use crate::recipe::generate_candidate;
use crate::recipe::AttemptOutcome;
use crayon_domain::{ActionKind, RiskLevel, SemanticNodeId, WorkflowTrace};

const ORIGIN: &str = "https://example.com";

fn candidate_trace() -> WorkflowTrace {
    WorkflowTrace::new(
        ORIGIN.to_owned(),
        vec![
            crayon_domain::TraceStep {
                node: SemanticNodeId::new("node1").unwrap(),
                action: ActionKind::Click,
                summary: "open form".to_owned(),
                outcome: crayon_domain::EffectOutcome::Verified,
            },
            crayon_domain::TraceStep {
                node: SemanticNodeId::new("node2").unwrap(),
                action: ActionKind::SetText,
                summary: "fill title".to_owned(),
                outcome: crayon_domain::EffectOutcome::Verified,
            },
        ],
    )
    .unwrap()
}

fn open_preview(now: u64) -> SkillPreviewController {
    let candidate = generate_candidate(
        AttemptOutcome::VerifiedSuccess,
        &candidate_trace(),
        "form-helper",
        1,
    )
    .expect("candidate");
    let crate::recipe::RecipeCandidate::Candidate(candidate) = candidate else {
        panic!("expected candidate");
    };
    SkillPreviewController::open(candidate.clone(), RiskLevel::R2, 1, now, now + 60_000)
        .expect("preview")
}

#[test]
fn preview_discloses_fields_and_data_flow() {
    let preview = open_preview(1_000);
    let data = preview.data();
    assert_eq!(data.recipe_name, "form-helper");
    assert_eq!(data.origin, ORIGIN);
    assert_eq!(data.version, 1);
    assert_eq!(data.step_count, 2);
    assert_eq!(data.step_summaries, ["open form", "fill title"]);
    assert_eq!(data.risk, RiskLevel::R2);
    // SetText => field-write disclosure; Click => activation disclosure.
    assert!(data.data_flow.writes_fields);
    assert!(data.data_flow.activates);
    assert!(!data.data_flow.changes_choices);
    assert_eq!(preview.phase(), PreviewPhase::Reviewing);
}

#[test]
fn confirm_is_single_consumption_releasing_the_candidate() {
    let mut preview = open_preview(1_000);
    let decision = preview.confirm(2_000).expect("confirm");
    let super::SaveDecision::Confirmed(recipe) = decision else {
        panic!("expected confirmed");
    };
    assert_eq!(recipe.name, "form-helper");
    assert_eq!(preview.phase(), PreviewPhase::Confirmed);
    // Second confirm is refused: already consumed.
    assert_eq!(preview.confirm(3_000), Err(PreviewError::SessionClosed));
}

#[test]
fn expired_preview_refuses_confirmation() {
    let mut preview = open_preview(1_000);
    // Past the TTL: confirmation refuses and the session expires.
    assert_eq!(preview.confirm(1_000 + 60_000), Err(PreviewError::Expired));
    assert_eq!(preview.phase(), PreviewPhase::Expired);
    assert_eq!(
        preview.confirm(1_000 + 60_000 + 1),
        Err(PreviewError::Expired)
    );
}

#[test]
fn ttl_bounds_are_validated_at_open() {
    let candidate = generate_candidate(
        AttemptOutcome::VerifiedSuccess,
        &candidate_trace(),
        "form-helper",
        1,
    )
    .unwrap();
    let crate::recipe::RecipeCandidate::Candidate(candidate) = candidate else {
        panic!("expected candidate");
    };
    // Expires before it opens.
    assert!(matches!(
        SkillPreviewController::open(candidate.clone(), RiskLevel::R2, 1, 1_000, 1_000),
        Err(PreviewError::InvalidTtl)
    ));
    // Beyond the TTL ceiling.
    assert!(matches!(
        SkillPreviewController::open(
            candidate.clone(),
            RiskLevel::R2,
            1,
            1_000,
            1_000 + super::MAX_PREVIEW_TTL_MS + 1
        ),
        Err(PreviewError::InvalidTtl)
    ));
}

#[test]
fn changed_candidate_invalidates_the_open_preview() {
    let preview = open_preview(1_000);
    let old_fingerprint = preview.fingerprint();

    let changed_trace = WorkflowTrace::new(
        ORIGIN.to_owned(),
        vec![crayon_domain::TraceStep {
            node: SemanticNodeId::new("node1").unwrap(),
            action: ActionKind::Click,
            summary: "changed step".to_owned(),
            outcome: crayon_domain::EffectOutcome::Verified,
        }],
    )
    .unwrap();
    let changed_candidate = generate_candidate(
        AttemptOutcome::VerifiedSuccess,
        &changed_trace,
        "form-helper",
        1,
    )
    .unwrap();
    let crate::recipe::RecipeCandidate::Candidate(changed) = changed_candidate else {
        panic!("expected candidate");
    };

    // The host computes fingerprints; a changed candidate must not match
    // the open preview, forcing a fresh preview and re-confirmation.
    let changed_controller =
        SkillPreviewController::open(changed.clone(), RiskLevel::R2, 1, 1_000, 61_000).unwrap();
    assert_ne!(old_fingerprint, changed_controller.fingerprint());
    assert!(!preview.matches_candidate(changed_controller.fingerprint()));
    assert!(preview.matches_candidate(old_fingerprint));
    // The old preview is still Reviewing — but only its OWN candidate can
    // be confirmed from it (no cross-candidate confirmation surface).
    assert_eq!(preview.phase(), PreviewPhase::Reviewing);
}

#[test]
fn reject_and_expire_are_idempotent_terminals() {
    let mut preview = open_preview(1_000);
    preview.reject();
    assert_eq!(preview.phase(), PreviewPhase::Rejected);
    preview.reject();
    assert_eq!(preview.phase(), PreviewPhase::Rejected);
    assert_eq!(preview.confirm(2_000), Err(PreviewError::SessionClosed));

    let mut preview2 = open_preview(1_000);
    preview2.expire();
    assert_eq!(preview2.phase(), PreviewPhase::Expired);
    preview2.expire();
    assert_eq!(preview2.phase(), PreviewPhase::Expired);
    assert_eq!(preview2.confirm(2_000), Err(PreviewError::Expired));
}

#[test]
fn disclosure_mapping_is_closed_per_action() {
    let trace = WorkflowTrace::new(
        ORIGIN.to_owned(),
        vec![crayon_domain::TraceStep {
            node: SemanticNodeId::new("n").unwrap(),
            action: ActionKind::SelectOption,
            summary: "choose".to_owned(),
            outcome: crayon_domain::EffectOutcome::Verified,
        }],
    )
    .unwrap();
    let candidate =
        generate_candidate(AttemptOutcome::VerifiedSuccess, &trace, "chooser", 1).unwrap();
    let crate::recipe::RecipeCandidate::Candidate(candidate) = candidate else {
        panic!("expected candidate");
    };
    let preview =
        SkillPreviewController::open(candidate.clone(), RiskLevel::R1, 1, 1_000, 61_000).unwrap();
    assert!(preview.data().data_flow.changes_choices);
    assert!(!preview.data().data_flow.writes_fields);
    assert!(!preview.data().data_flow.activates);
}
