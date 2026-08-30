use crate::redaction::{
    redact_for_persistence, ParameterClass, RedactionError, WorkflowParameter,
    MAX_REDACTED_PARAMETERS,
};
use crayon_domain::{ActionKind, EffectOutcome, SemanticNodeId, TraceStep, WorkflowTrace};

fn trace(summary: &str) -> WorkflowTrace {
    WorkflowTrace::new(
        "https://example.com".to_owned(),
        vec![TraceStep {
            node: SemanticNodeId::new("node-7").expect("node"),
            action: ActionKind::SetText,
            summary: summary.to_owned(),
            outcome: EffectOutcome::Verified,
        }],
    )
    .expect("trace")
}

#[test]
fn seeded_sensitive_matrix_has_zero_serialized_leakage() {
    let values = [
        ("password", "pw-canary-93", ParameterClass::Secret),
        ("email", "alice-canary@example.test", ParameterClass::Email),
        ("token", "tok_canary_117", ParameterClass::Secret),
        (
            "body",
            "private body canary sentence",
            ParameterClass::UserContent,
        ),
        (
            "source_url",
            "https://example.test/path?account=canary&token=secret",
            ParameterClass::Url,
        ),
        (
            "account",
            "account-canary-42",
            ParameterClass::AccountIdentifier,
        ),
    ];
    let parameters: Vec<_> = values
        .iter()
        .map(|(name, value, class)| WorkflowParameter {
            name,
            value,
            class: *class,
        })
        .collect();
    let output = redact_for_persistence(&trace("private body canary sentence"), &parameters)
        .expect("redacted");
    let wire = serde_json::to_string(&output).expect("serialize");
    for (_, canary, _) in values {
        assert!(!wire.contains(canary), "leaked canary: {canary}");
    }
    assert!(!wire.contains("?account="));
    assert!(!wire.contains("private body"));
    assert_eq!(output.trace.steps[0].summary, "set_text");
    assert_eq!(output.parameters.len(), 6);
}

#[test]
fn wrong_class_still_cannot_preserve_a_value() {
    let parameters = [WorkflowParameter {
        name: "label",
        value: "secret-misclassified-canary",
        class: ParameterClass::Text,
    }];
    let output = redact_for_persistence(&trace("set_text"), &parameters).expect("redacted");
    let wire = serde_json::to_string(&output).expect("serialize");
    assert!(!wire.contains("secret-misclassified-canary"));
    assert!(wire.contains("\"class\":\"text\""));
}

#[test]
fn output_has_no_value_length_digest_or_hash_surface() {
    let output = redact_for_persistence(
        &trace("set_text"),
        &[WorkflowParameter {
            name: "account",
            value: "low-entropy-account",
            class: ParameterClass::AccountIdentifier,
        }],
    )
    .expect("redacted");
    let wire = serde_json::to_string(&output).expect("serialize");
    for forbidden in ["value", "length", "digest", "hash", "low-entropy-account"] {
        assert!(!wire.contains(forbidden));
    }
}

#[test]
fn invalid_duplicate_and_over_capacity_names_fail_closed() {
    for name in ["", "UPPER", "email@account", "bad name"] {
        assert_eq!(
            redact_for_persistence(
                &trace("set_text"),
                &[WorkflowParameter {
                    name,
                    value: "canary",
                    class: ParameterClass::Text,
                }]
            ),
            Err(RedactionError::ParameterNameInvalid)
        );
    }
    let duplicate = [
        WorkflowParameter {
            name: "field",
            value: "one",
            class: ParameterClass::Text,
        },
        WorkflowParameter {
            name: "field",
            value: "two",
            class: ParameterClass::Secret,
        },
    ];
    assert_eq!(
        redact_for_persistence(&trace("set_text"), &duplicate),
        Err(RedactionError::DuplicateParameter)
    );
    let many: Vec<_> = (0..=MAX_REDACTED_PARAMETERS)
        .map(|_| WorkflowParameter {
            name: "field",
            value: "canary",
            class: ParameterClass::Text,
        })
        .collect();
    assert_eq!(
        redact_for_persistence(&trace("set_text"), &many),
        Err(RedactionError::ParameterCapacity)
    );
}

#[test]
fn wrong_schema_and_non_verified_trace_fail_closed() {
    let mut wrong_schema = trace("set_text");
    wrong_schema.schema_version += 1;
    assert_eq!(
        redact_for_persistence(&wrong_schema, &[]),
        Err(RedactionError::WrongSchema)
    );
    let mut failed = trace("set_text");
    failed.steps[0].outcome = EffectOutcome::Failed;
    assert_eq!(
        redact_for_persistence(&failed, &[]),
        Err(RedactionError::TraceNotVerified)
    );
}

#[test]
fn empty_parameter_set_is_valid_and_deterministic() {
    let input = trace("caller-controlled-canary");
    let first = redact_for_persistence(&input, &[]).expect("redacted");
    let second = redact_for_persistence(&input, &[]).expect("redacted");
    assert_eq!(first, second);
    assert!(first.parameters.is_empty());
    assert_eq!(first.trace.steps[0].summary, "set_text");
}
