//! Local skill validation against closed fixtures (WFL-11).
//!
//! Validates a saved skill's steps and a run's reported effects against a
//! caller-supplied local fixture. Pure read-only functions over closed
//! inputs: no network, no background work, no mutation of the skill.

use std::collections::BTreeSet;

use crayon_domain::{ActionKind, EffectOutcome, SemanticNodeId, SiteSkill};

/// Maximum steps a validated skill may declare (recipe budget parity).
pub const MAX_VALIDATED_STEPS: usize = 64;

/// One fixture node: a known node id and the actions the local sandbox
/// confirms it supports.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FixtureNode {
    pub id: SemanticNodeId,
    pub supported_actions: BTreeSet<ActionKind>,
}

impl FixtureNode {
    #[must_use]
    pub fn new(id: SemanticNodeId, supported: impl IntoIterator<Item = ActionKind>) -> Self {
        Self {
            id,
            supported_actions: supported.into_iter().collect(),
        }
    }
}

/// Local sandbox fixture: closed node inventory plus the origin the skill
/// must match. No network access exists on this type.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ValidationFixture {
    origin: String,
    nodes: BTreeSet<SemanticNodeId>,
    supported: BTreeMap<SemanticNodeId, BTreeSet<ActionKind>>,
}

use std::collections::BTreeMap;

impl ValidationFixture {
    /// Builds a fixture for one origin from known nodes. The origin must
    /// pass the closed `http(s)` validity check, matching the skill rule.
    pub fn new(
        origin: &str,
        nodes: impl IntoIterator<Item = FixtureNode>,
    ) -> Result<Self, ValidationError> {
        if !crayon_domain::is_valid_origin(origin) {
            return Err(ValidationError::OriginMismatch);
        }
        let mut fixture = Self {
            origin: origin.to_owned(),
            nodes: BTreeSet::new(),
            supported: BTreeMap::new(),
        };
        for node in nodes {
            fixture.nodes.insert(node.id.clone());
            fixture.supported.insert(node.id, node.supported_actions);
        }
        Ok(fixture)
    }

    #[must_use]
    pub fn origin(&self) -> &str {
        &self.origin
    }

    #[must_use]
    pub fn knows(&self, node: &SemanticNodeId) -> bool {
        self.nodes.contains(node)
    }

    #[must_use]
    pub fn supports(&self, node: &SemanticNodeId, action: ActionKind) -> bool {
        self.supported
            .get(node)
            .is_some_and(|actions| actions.contains(&action))
    }
}

/// Why validation refused a skill. Closed, content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ValidationError {
    /// The skill origin differs from the fixture origin.
    OriginMismatch,
    /// The skill has no steps.
    EmptySkill,
    /// More steps than the validation budget allows.
    StepBudgetExceeded,
    /// A step references a node the fixture does not know.
    NodeUnknown,
    /// A step uses an action the fixture node does not support.
    ActionUnsupported,
    /// The reported effect does not line up with the expected step
    /// (node/action mismatch, unverified outcome, count or order drift).
    EffectMismatch,
}

/// Validates a saved skill against the local fixture. Read-only; the
/// skill is not modified, executed or learned from.
pub fn validate_skill(
    skill: &SiteSkill,
    fixture: &ValidationFixture,
) -> Result<(), ValidationError> {
    if skill.recipe.origin != fixture.origin() {
        return Err(ValidationError::OriginMismatch);
    }
    if skill.recipe.steps.is_empty() {
        return Err(ValidationError::EmptySkill);
    }
    if skill.recipe.steps.len() > MAX_VALIDATED_STEPS {
        return Err(ValidationError::StepBudgetExceeded);
    }
    for step in &skill.recipe.steps {
        if !fixture.knows(&step.node) {
            return Err(ValidationError::NodeUnknown);
        }
        if !fixture.supports(&step.node, step.action) {
            return Err(ValidationError::ActionUnsupported);
        }
    }
    Ok(())
}

/// Effect-validation verdict for one executed step. Single judgment:
/// the reported effect matches exactly when node and action agree and
/// the outcome is verified.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ReportedEffect {
    pub node: SemanticNodeId,
    pub action: ActionKind,
    pub outcome: EffectOutcome,
}

/// The expected shape of one step's effect, taken from the skill.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ExpectedEffect {
    pub node: SemanticNodeId,
    pub action: ActionKind,
}

/// Validates one reported effect against its expectation and the fixture.
/// `Failed`/`Indeterminate` outcomes never validate as success.
pub fn validate_effect(
    expected: &ExpectedEffect,
    reported: &ReportedEffect,
    fixture: &ValidationFixture,
) -> Result<(), ValidationError> {
    if expected.node != reported.node || expected.action != reported.action {
        return Err(ValidationError::EffectMismatch);
    }
    if reported.outcome != EffectOutcome::Verified {
        return Err(ValidationError::EffectMismatch);
    }
    if !fixture.knows(&reported.node) {
        return Err(ValidationError::NodeUnknown);
    }
    if !fixture.supports(&reported.node, reported.action) {
        return Err(ValidationError::ActionUnsupported);
    }
    Ok(())
}

/// Validates a whole run: reported effects must line up with the skill's
/// steps in order (1:1, no extras, no gaps).
pub fn validate_run(
    skill: &SiteSkill,
    reported: &[ReportedEffect],
    fixture: &ValidationFixture,
) -> Result<(), ValidationError> {
    validate_skill(skill, fixture)?;
    if reported.len() != skill.recipe.steps.len() {
        return Err(ValidationError::EffectMismatch);
    }
    for (step, effect) in skill.recipe.steps.iter().zip(reported) {
        validate_effect(
            &ExpectedEffect {
                node: step.node.clone(),
                action: step.action,
            },
            effect,
            fixture,
        )?;
    }
    Ok(())
}

#[cfg(test)]
mod validation_tests;
