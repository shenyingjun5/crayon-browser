//! Workflow schema vocabulary (WFL-01, WF-001 契约侧).
//!
//! The frozen v1 data shapes for the workflow learning family: semantic
//! traces, candidate recipes, personal site skills, challenge sessions
//! and checkpoints. Everything here is platform-independent domain data —
//! no selectors, no field values, no credentials, no challenge-solving
//! surfaces. Extending the closed sets is a protocol-versioned change.

mod challenge;
mod checkpoint;
mod recipe;
mod skill;
mod trace;

pub use challenge::{
    ChallengeEvidence, ChallengeKind, ChallengePhase, ChallengeSession, ChallengeTransitionError,
    MAX_CHALLENGE_EVIDENCE_BYTES,
};
pub use checkpoint::{
    Checkpoint, CheckpointError, CheckpointState, MAX_CHECKPOINT_PAYLOAD_BYTES,
    MAX_CHECKPOINT_TTL_MS,
};
pub use recipe::{
    Recipe, RecipeError, RecipeStep, MAX_RECIPE_NAME_BYTES, MAX_RECIPE_STEPS, MAX_RECIPE_VERSION,
};
pub use skill::{SiteSkill, SkillError, SkillStatus, MAX_SKILL_REVISION};
pub use trace::{
    TraceError, TraceStep, WorkflowTrace, MAX_TRACE_ORIGIN_BYTES, MAX_TRACE_STEPS,
    MAX_TRACE_SUMMARY_BYTES,
};

/// Frozen v1 schema version of the workflow family.
pub const WORKFLOW_SCHEMA_VERSION: u32 = 1;
