//! WF-012 coverage: Enabled gating, per-step fresh authorization,
//! ordered execution with effect alignment, cancellation, deadline,
//! port failure and challenge termination.

use super::{RunCancel, RunOutcome, RunTermination, RunnerPort, SkillRunner};
use crate::validation::{FixtureNode, ValidationFixture};
use crayon_domain::{
    ActionKind, CaapError, EffectOutcome, Recipe, SemanticNodeId, SessionGeneration, SiteSkill,
    SkillStatus, TabId,
};
use crayon_semantic_action::{ApprovedAction, ConfirmationRef};

const ORIGIN: &str = "https://example.com";

fn enabled_skill(actions: &[ActionKind]) -> SiteSkill {
    let steps: Vec<crayon_domain::RecipeStep> = actions
        .iter()
        .enumerate()
        .map(|(index, action)| crayon_domain::RecipeStep {
            node: SemanticNodeId::new(&format!("node{}", index + 1)).unwrap(),
            action: *action,
            summary: format!("step {}", index + 1),
        })
        .collect();
    let recipe = Recipe::new(ORIGIN.to_owned(), "runner-skill", 1, steps).unwrap();
    SiteSkill::new(recipe, SkillStatus::Enabled, 1).unwrap()
}

fn fixture_for(actions: &[ActionKind]) -> ValidationFixture {
    let nodes: Vec<FixtureNode> = actions
        .iter()
        .enumerate()
        .map(|(index, action)| {
            FixtureNode::new(
                SemanticNodeId::new(&format!("node{}", index + 1)).unwrap(),
                std::iter::once(*action),
            )
        })
        .collect();
    ValidationFixture::new(ORIGIN, nodes).unwrap()
}

struct RecordingPort {
    outcomes: Vec<Result<EffectOutcome, CaapError>>,
    executed: Vec<SemanticNodeId>,
}

impl RunnerPort for RecordingPort {
    fn execute_step(
        &mut self,
        approved: &ApprovedAction,
        _cancel: &RunCancel,
    ) -> Result<crayon_domain::EffectReport, CaapError> {
        self.executed.push(approved.node.clone());
        match self.outcomes.pop().unwrap_or(Err(CaapError::TargetInvalid)) {
            Ok(outcome) => Ok(crayon_domain::EffectReport {
                schema_version: crayon_domain::SEMANTIC_MAP_SCHEMA_VERSION,
                tab_id: approved.tab_id.clone(),
                generation: approved.generation,
                revision: 1,
                action: approved.action,
                node: approved.node.clone(),
                outcome,
                reason: None,
                detail: None,
            }),
            Err(error) => Err(error),
        }
    }
}

fn approved_for(
    index: usize,
    action: ActionKind,
    tab: &TabId,
    gen: SessionGeneration,
    now: u64,
) -> ApprovedAction {
    ApprovedAction {
        node: SemanticNodeId::new(&format!("node{}", index + 1)).unwrap(),
        action,
        tab_id: tab.clone(),
        generation: gen,
        deadline_ms: now + 30_000,
        confirmation: ConfirmationRef::new("confirm-1").unwrap(),
    }
}

#[test]
fn non_enabled_skill_refuses_to_run() {
    let actions = [ActionKind::Click];
    let recipe = Recipe::new(
        ORIGIN.to_owned(),
        "runner-skill",
        1,
        vec![crayon_domain::RecipeStep {
            node: SemanticNodeId::new("node1").unwrap(),
            action: ActionKind::Click,
            summary: "step 1".to_owned(),
        }],
    )
    .unwrap();
    let skill = SiteSkill::new(recipe, SkillStatus::Draft, 1).unwrap();
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let cancel = RunCancel::new();
    let mut approvals = |_index: usize| {
        Some(approved_for(
            0,
            ActionKind::Click,
            &TabId::new("tab-1").unwrap(),
            SessionGeneration::from_raw(1),
            1_000,
        ))
    };
    let outcome = runner.run(&mut port, &mut approvals, &cancel, || false, || 1_000);
    assert_eq!(outcome, RunOutcome::Terminated(RunTermination::StepFailed));
    assert!(port.executed.is_empty());
}

#[test]
fn happy_run_executes_in_order_and_completes() {
    let actions = [ActionKind::Click, ActionKind::SetText];
    let skill = enabled_skill(&actions);
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![Ok(EffectOutcome::Verified), Ok(EffectOutcome::Verified)],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let cancel = RunCancel::new();
    let mut counter = 0usize;
    let mut approvals = |index: usize| {
        assert_eq!(index, counter, "approvals must be requested in order");
        counter += 1;
        Some(approved_for(
            index,
            actions[index],
            &TabId::new("tab-1").unwrap(),
            SessionGeneration::from_raw(1),
            1_000,
        ))
    };
    let outcome = runner.run(&mut port, &mut approvals, &cancel, || false, || 1_000);
    assert_eq!(outcome, RunOutcome::Completed);
    assert_eq!(port.executed.len(), 2);
    assert_eq!(port.executed[0].as_str(), "node1");
    assert_eq!(port.executed[1].as_str(), "node2");
}

#[test]
fn missing_approval_and_port_failure_terminate_fail_closed() {
    // Missing approval.
    let actions = [ActionKind::Click];
    let skill = enabled_skill(&actions);
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let cancel = RunCancel::new();
    let mut no_approvals = |_index: usize| None;
    assert_eq!(
        runner.run(&mut port, &mut no_approvals, &cancel, || false, || 1_000),
        RunOutcome::Terminated(RunTermination::StepFailed)
    );

    // Port failure.
    let mut failing = RecordingPort {
        outcomes: vec![Err(CaapError::TargetInvalid)],
        executed: vec![],
    };
    let runner2 = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let mut fresh = |_index: usize| {
        Some(approved_for(
            0,
            ActionKind::Click,
            &TabId::new("tab-1").unwrap(),
            SessionGeneration::from_raw(1),
            1_000,
        ))
    };
    assert_eq!(
        runner2.run(&mut failing, &mut fresh, &cancel, || false, || 1_000),
        RunOutcome::Terminated(RunTermination::StepFailed)
    );
}

#[test]
fn cancellation_stops_before_next_step() {
    let actions = [ActionKind::Click, ActionKind::SetText];
    let skill = enabled_skill(&actions);
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![Ok(EffectOutcome::Verified), Ok(EffectOutcome::Verified)],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let cancel = RunCancel::new();
    cancel.cancel();
    let mut approvals = |_index: usize| {
        Some(approved_for(
            0,
            ActionKind::Click,
            &TabId::new("tab-1").unwrap(),
            SessionGeneration::from_raw(1),
            1_000,
        ))
    };
    // External cancellation arriving between steps stops the run before
    // the next step executes.
    let outcome = runner.run(&mut port, &mut approvals, &cancel, || false, || 1_000);
    assert_eq!(outcome, RunOutcome::Terminated(RunTermination::Cancelled));
    assert!(port.executed.is_empty());
}

#[test]
fn deadline_expiry_terminates() {
    let actions = [ActionKind::Click];
    let skill = enabled_skill(&actions);
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        5_000,
    );
    let cancel = RunCancel::new();
    let mut approvals = |_index: usize| {
        Some(approved_for(
            0,
            ActionKind::Click,
            &TabId::new("tab-1").unwrap(),
            SessionGeneration::from_raw(1),
            5_000,
        ))
    };
    assert_eq!(
        runner.run(&mut port, &mut approvals, &cancel, || false, || 6_000),
        RunOutcome::Terminated(RunTermination::DeadlineExceeded)
    );
}

#[test]
fn challenge_detection_terminates_immediately() {
    let actions = [ActionKind::Click, ActionKind::SetText];
    let skill = enabled_skill(&actions);
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![Ok(EffectOutcome::Verified), Ok(EffectOutcome::Verified)],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let cancel = RunCancel::new();
    let mut approvals = |index: usize| {
        Some(approved_for(
            index,
            actions[index],
            &TabId::new("tab-1").unwrap(),
            SessionGeneration::from_raw(1),
            1_000,
        ))
    };
    let challenge = || true; // detector raised mid-run
    let outcome = runner.run(&mut port, &mut approvals, &cancel, challenge, || 1_000);
    assert_eq!(
        outcome,
        RunOutcome::Terminated(RunTermination::ChallengeDetected)
    );
    assert!(port.executed.is_empty());
}

#[test]
fn mismatched_approval_is_rejected_before_execution() {
    let actions = [ActionKind::Click, ActionKind::SetText];
    let skill = enabled_skill(&actions);
    let fixture = fixture_for(&actions);
    let mut port = RecordingPort {
        outcomes: vec![],
        executed: vec![],
    };
    let runner = SkillRunner::prepare(
        &skill,
        &fixture,
        TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        100_000,
    );
    let cancel = RunCancel::new();
    // Reuse the same approval for both steps: step 2 must be rejected.
    let reused = approved_for(
        0,
        ActionKind::Click,
        &TabId::new("tab-1").unwrap(),
        SessionGeneration::from_raw(1),
        1_000,
    );
    let mut approvals = |_index: usize| Some(reused.clone());
    assert_eq!(
        runner.run(&mut port, &mut approvals, &cancel, || false, || 1_000),
        RunOutcome::Terminated(RunTermination::StepFailed)
    );
    assert_eq!(port.executed.len(), 1); // only step 1 executed
}
