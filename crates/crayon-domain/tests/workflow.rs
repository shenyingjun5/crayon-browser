//! Workflow schema tests (WFL-01, WF-001 契约侧): closed vocabularies,
//! budget enforcement, no-secret/value surfaces, challenge state machine
//! with no solving path, single-use checkpoints and stable wire forms.

use crayon_domain::{
    ActionKind, ChallengeEvidence, ChallengeKind, ChallengePhase, ChallengeSession,
    ChallengeTransitionError, Checkpoint, CheckpointError, CheckpointState, EffectOutcome, Recipe,
    RecipeError, RecipeStep, SemanticNodeId, SemanticSchemaError, SessionGeneration, SiteSkill,
    SkillStatus, TabId, TraceError, TraceStep, WorkflowTrace, MAX_CHECKPOINT_PAYLOAD_BYTES,
    MAX_CHECKPOINT_TTL_MS, MAX_RECIPE_STEPS, MAX_TRACE_STEPS, WORKFLOW_SCHEMA_VERSION,
};

fn node_id(raw: &str) -> SemanticNodeId {
    SemanticNodeId::new(raw).expect("valid node id")
}

fn step(summary: &str, outcome: EffectOutcome) -> TraceStep {
    TraceStep {
        node: node_id("n-1"),
        action: ActionKind::Click,
        summary: summary.to_owned(),
        outcome,
    }
}

// ---------- Trace ----------

#[test]
fn traces_are_bounded_and_free_of_value_surfaces() {
    let trace = WorkflowTrace::new(
        "https://example.com".to_owned(),
        vec![step("提交订单按钮", EffectOutcome::Verified)],
    )
    .expect("valid trace");
    assert_eq!(trace.schema_version, WORKFLOW_SCHEMA_VERSION);
    let wire = serde_json::to_string(&trace).expect("serialize");
    for forbidden in [
        "selector",
        "value",
        "\"html\"",
        "\"dom\"",
        "cookie",
        "authorization",
    ] {
        assert!(
            !wire.to_ascii_lowercase().contains(forbidden),
            "{forbidden} leaked"
        );
    }
    // Budgets fail closed.
    assert_eq!(
        WorkflowTrace::new("https://example.com".to_owned(), {
            let mut steps = Vec::new();
            for index in 0..=MAX_TRACE_STEPS {
                steps.push(step(&format!("step-{index}"), EffectOutcome::Verified));
            }
            steps
        }),
        Err(TraceError::StepBudgetExceeded)
    );
    assert_eq!(
        WorkflowTrace::new("https://example.com/path".to_owned(), Vec::new()),
        Err(TraceError::OriginInvalid)
    );
    assert_eq!(
        WorkflowTrace::new(
            "https://example.com".to_owned(),
            vec![step(&"x".repeat(129), EffectOutcome::Verified)]
        ),
        Err(TraceError::SummaryTooLong)
    );
}

// ---------- Recipe and skill ----------

#[test]
fn recipes_carry_intent_only_with_closed_names_and_versions() {
    let recipe = Recipe::new(
        "https://example.com".to_owned(),
        "order-flow",
        1,
        vec![RecipeStep {
            node: node_id("n-1"),
            action: ActionKind::Click,
            summary: "submit".to_owned(),
        }],
    )
    .expect("valid recipe");
    assert_eq!(recipe.schema_version, WORKFLOW_SCHEMA_VERSION);
    assert_eq!(
        Recipe::new("https://example.com".to_owned(), "Bad Name!", 1, Vec::new()),
        Err(RecipeError::NameInvalid)
    );
    assert_eq!(
        Recipe::new("https://example.com".to_owned(), "ok", 0, Vec::new()),
        Err(RecipeError::VersionOutOfBounds)
    );
    assert_eq!(
        Recipe::new(
            "https://example.com".to_owned(),
            "ok",
            1,
            (0..=MAX_RECIPE_STEPS)
                .map(|_index| RecipeStep {
                    node: node_id("n-1"),
                    action: ActionKind::Click,
                    summary: String::new(),
                })
                .collect()
        ),
        Err(RecipeError::StepBudgetExceeded)
    );
}

#[test]
fn skills_run_only_when_enabled_and_revision_is_bounded() {
    let recipe = Recipe::new(
        "https://example.com".to_owned(),
        "order-flow",
        1,
        Vec::new(),
    )
    .expect("valid recipe");
    let draft = SiteSkill::new(recipe.clone(), SkillStatus::Draft, 1).expect("valid skill");
    assert!(!draft.runnable(), "draft skills never run");
    let enabled = SiteSkill::new(recipe.clone(), SkillStatus::Enabled, 1).expect("valid skill");
    assert!(enabled.runnable());
    assert!(!SiteSkill::new(recipe, SkillStatus::Disabled, 1)
        .expect("valid skill")
        .runnable());
    assert_eq!(
        SiteSkill::new(
            Recipe::new("https://example.com".to_owned(), "x", 1, Vec::new()).expect("recipe"),
            SkillStatus::Enabled,
            0
        ),
        Err(crayon_domain::SkillError::RevisionOutOfBounds)
    );
}

// ---------- Challenge session ----------

#[test]
fn challenge_sessions_follow_the_closed_phase_machine() {
    let evidence = ChallengeEvidence::new(
        ChallengeKind::Captcha,
        "https://example.com".to_owned(),
        None,
    )
    .expect("valid evidence");
    let mut session = ChallengeSession::detect(evidence).expect("session");
    assert_eq!(session.phase, ChallengePhase::Detected);
    session.await_human().expect("pause");
    assert_eq!(session.phase, ChallengePhase::AwaitingHuman);
    // Only one terminal from AwaitingHuman.
    session.resume().expect("resume");
    assert!(session.closed());
    // A closed session rejects every transition.
    assert_eq!(
        session.cancel(),
        Err(ChallengeTransitionError::SessionClosed)
    );
    assert_eq!(
        session.expire(),
        Err(ChallengeTransitionError::SessionClosed)
    );
    assert_eq!(
        session.resume(),
        Err(ChallengeTransitionError::SessionClosed)
    );
}

#[test]
fn challenge_transitions_never_return_to_automation() {
    // Detected cannot jump to a terminal or resume directly.
    let evidence = ChallengeEvidence::new(
        ChallengeKind::RiskCheck,
        "https://example.com".to_owned(),
        None,
    )
    .expect("valid evidence");
    let mut session = ChallengeSession::detect(evidence).expect("session");
    assert_eq!(
        session.resume(),
        Err(ChallengeTransitionError::IllegalTransition)
    );
    assert_eq!(
        session.cancel(),
        Err(ChallengeTransitionError::IllegalTransition)
    );
    // AwaitingHuman may expire.
    let evidence = ChallengeEvidence::new(
        ChallengeKind::LoginRequired,
        "https://example.com".to_owned(),
        None,
    )
    .expect("valid evidence");
    let mut session = ChallengeSession::detect(evidence).expect("session");
    session.await_human().expect("pause");
    session.expire().expect("expire");
    assert_eq!(session.phase, ChallengePhase::Expired);
    // Evidence notes are bounded.
    assert_eq!(
        ChallengeEvidence::new(
            ChallengeKind::Unknown,
            "https://example.com".to_owned(),
            Some("x".repeat(129))
        ),
        Err(SemanticSchemaError::BoundExceeded("challenge evidence"))
    );
    // Wire form has no solving surface.
    let wire = serde_json::to_string(&session).expect("serialize");
    for forbidden in ["solution", "answer", "token", "solver", "bypass"] {
        assert!(
            !wire.to_ascii_lowercase().contains(forbidden),
            "{forbidden} leaked"
        );
    }
}

// ---------- Checkpoint ----------

#[test]
fn checkpoints_are_short_lived_and_single_use() {
    let mut checkpoint = Checkpoint::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        42,
        vec![7, 42, 1],
        1_000,
        61_000,
    )
    .expect("valid checkpoint");
    assert_eq!(checkpoint.schema_version, WORKFLOW_SCHEMA_VERSION);
    assert!(!checkpoint.expired_at(60_999));
    checkpoint.consume(2_000).expect("consume once");
    assert_eq!(checkpoint.state, CheckpointState::Consumed);
    // Single use: replay is rejected.
    assert_eq!(checkpoint.consume(2_000), Err(CheckpointError::NotLive));
    // TTL bounds fail closed.
    assert_eq!(
        Checkpoint::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(3),
            42,
            Vec::new(),
            1_000,
            1_000
        ),
        Err(CheckpointError::TtlOutOfBounds)
    );
    assert_eq!(
        Checkpoint::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(3),
            42,
            Vec::new(),
            1_000,
            1_001 + MAX_CHECKPOINT_TTL_MS
        ),
        Err(CheckpointError::TtlOutOfBounds)
    );
    // Payload bound fails closed.
    assert_eq!(
        Checkpoint::new(
            TabId::new("tab-1").expect("tab id"),
            SessionGeneration::from_raw(3),
            42,
            vec![0u8; MAX_CHECKPOINT_PAYLOAD_BYTES + 1],
            1_000,
            61_000
        ),
        Err(CheckpointError::PayloadTooLarge)
    );
    // Expiry transition.
    let mut checkpoint = Checkpoint::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        42,
        Vec::new(),
        1_000,
        61_000,
    )
    .expect("valid");
    checkpoint.mark_expired(61_000).expect("expire");
    assert_eq!(checkpoint.state, CheckpointState::Expired);
    // Discard.
    let mut checkpoint = Checkpoint::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(3),
        42,
        Vec::new(),
        1_000,
        61_000,
    )
    .expect("valid");
    checkpoint.discard().expect("discard");
    assert_eq!(checkpoint.state, CheckpointState::Discarded);
    assert_eq!(checkpoint.consume(2_000), Err(CheckpointError::NotLive));
}
