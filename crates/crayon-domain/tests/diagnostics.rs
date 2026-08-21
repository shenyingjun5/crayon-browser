//! Behaviour tests for diagnostics classification, redaction, the event
//! schema and the bounded producer (RL-014, PV-008, PV-010).

use crayon_domain::{
    redact_sensitive, DataClass, DiagnosticError, DiagnosticEvent, DiagnosticProducer,
    DEFAULT_QUEUE_CAPACITY, MAX_ATTRIBUTES_PER_EVENT,
};

// ---------- Classification ----------

#[test]
fn user_content_and_secrets_never_enter_diagnostics() {
    assert!(DataClass::Operational.permits_diagnostics());
    assert!(DataClass::Diagnostic.permits_diagnostics());
    assert!(!DataClass::UserContent.permits_diagnostics());
    assert!(!DataClass::Secret.permits_diagnostics());
    assert_eq!(
        DiagnosticEvent::new(DataClass::UserContent, "page.view", 1),
        Err(DiagnosticError::ForbiddenClass)
    );
    assert_eq!(
        DiagnosticEvent::new(DataClass::Secret, "credential.read", 1),
        Err(DiagnosticError::ForbiddenClass)
    );
}

#[test]
fn name_and_key_shape_is_enforced() {
    assert!(DiagnosticEvent::new(DataClass::Operational, "nav.commit", 1).is_ok());
    assert!(DiagnosticEvent::new(DataClass::Operational, "a:b-c_d", 1).is_ok());
    // Empty, overlong, uppercase and exotic characters are rejected.
    assert_eq!(
        DiagnosticEvent::new(DataClass::Operational, "", 1),
        Err(DiagnosticError::InvalidName)
    );
    assert_eq!(
        DiagnosticEvent::new(DataClass::Operational, &"n".repeat(65), 1),
        Err(DiagnosticError::InvalidName)
    );
    assert_eq!(
        DiagnosticEvent::new(DataClass::Operational, "Nav.Commit", 1),
        Err(DiagnosticError::InvalidName)
    );
    assert_eq!(
        DiagnosticEvent::new(DataClass::Operational, "bad name", 1),
        Err(DiagnosticError::InvalidName)
    );
    let event = DiagnosticEvent::new(DataClass::Operational, "e", 1).expect("event");
    assert_eq!(
        event.clone().with_attribute("Bad Key", "v"),
        Err(DiagnosticError::InvalidAttributeKey)
    );
    assert_eq!(
        event.with_attribute(&"k".repeat(33), "v"),
        Err(DiagnosticError::InvalidAttributeKey)
    );
}

#[test]
fn attribute_capacity_and_value_bounds() {
    let mut event = DiagnosticEvent::new(DataClass::Diagnostic, "e", 1).expect("event");
    for index in 0..MAX_ATTRIBUTES_PER_EVENT {
        event = event
            .with_attribute(&format!("key{index}"), "value")
            .expect("attribute");
    }
    assert_eq!(
        event.with_attribute("overflow", "v"),
        Err(DiagnosticError::AttributeCapacity)
    );
    let event = DiagnosticEvent::new(DataClass::Diagnostic, "e", 1).expect("event");
    assert_eq!(
        event.with_attribute("k", &"v".repeat(257)),
        Err(DiagnosticError::InvalidAttributeValue)
    );
}

// ---------- Redaction (RL-014) ----------

#[test]
fn redaction_scrubs_url_query_and_userinfo() {
    assert_eq!(
        redact_sensitive("open https://example.com/watch?v=abc&token=xyz end"),
        "open https://example.com/watch[redacted] end"
    );
    assert_eq!(
        redact_sensitive("https://user:pass@example.com/path"),
        "https://[redacted]@example.com/path"
    );
    // No query, no userinfo: the URL passes through unchanged.
    assert_eq!(
        redact_sensitive("see https://example.com/docs/page"),
        "see https://example.com/docs/page"
    );
}

#[test]
fn redaction_scrubs_credential_headers() {
    assert_eq!(
        redact_sensitive("Authorization: Bearer abc.def"),
        "Authorization:[redacted]"
    );
    assert_eq!(
        redact_sensitive("cookie: SESSION=abc; theme=light"),
        "cookie:[redacted]"
    );
    assert_eq!(
        redact_sensitive("Set-Cookie: a=b\nProxy-Authorization: Basic c2Vj"),
        "Set-Cookie:[redacted]\nProxy-Authorization:[redacted]"
    );
}

#[test]
fn redaction_scrubs_tokens_and_params() {
    // The key/marker is kept, only the secret value is scrubbed.
    assert_eq!(
        redact_sensitive("prefix Bearer abc.def-123 suffix"),
        "prefix Bearer [redacted] suffix"
    );
    assert_eq!(redact_sensitive("Basic c2VjcmV0"), "Basic [redacted]");
    assert_eq!(
        redact_sensitive("POST /x?token=abc123&keep=1"),
        "POST /x?token=[redacted]&keep=1"
    );
    assert_eq!(
        redact_sensitive("sign=deadbeef rest"),
        "sign=[redacted] rest"
    );
    assert_eq!(redact_sensitive("sessdata=ABC"), "sessdata=[redacted]");
}

#[test]
fn redaction_leaves_benign_text_unchanged() {
    let benign = "tab 42 closed after 3 navigations; error=playback_not_advanced";
    assert_eq!(redact_sensitive(benign), benign);
    assert_eq!(redact_sensitive(""), "");
    // "assigned=" must not trip the "sign=" parameter rule.
    assert_eq!(redact_sensitive("assigned=1"), "assigned=1");
}

#[test]
fn attribute_values_are_redacted_on_insert() {
    let event = DiagnosticEvent::new(DataClass::Diagnostic, "e", 1)
        .expect("event")
        .with_attribute("last", "https://a.example/x?token=secret")
        .expect("attribute");
    assert_eq!(
        event.attributes().get("last").expect("key"),
        "https://a.example/x[redacted]"
    );
}

// ---------- Wire schema ----------

#[test]
fn event_wire_roundtrip_and_schema_rules() {
    let event = DiagnosticEvent::new(DataClass::Operational, "nav.commit", 42)
        .expect("event")
        .with_attribute("tab", "7")
        .expect("attribute");
    let json = serde_json::to_string(&event).expect("serialize");
    assert!(json.contains("\"schema\":1"));
    let decoded: DiagnosticEvent = serde_json::from_str(&json).expect("decode");
    assert_eq!(decoded, event);
    assert!(decoded.validate().is_ok());

    // Unknown fields are rejected.
    let with_unknown = json.replace("\"schema\":1", "\"schema\":1,\"extra\":true");
    assert!(serde_json::from_str::<DiagnosticEvent>(&with_unknown).is_err());

    // A wrong schema version fails validation.
    let wrong_version = json.replace("\"schema\":1", "\"schema\":2");
    let decoded: DiagnosticEvent = serde_json::from_str(&wrong_version).expect("decode");
    assert_eq!(decoded.validate(), Err(DiagnosticError::InvalidEvent));

    // A forbidden class in the payload fails validation.
    let forbidden = json.replace("\"operational\"", "\"user_content\"");
    let decoded: DiagnosticEvent = serde_json::from_str(&forbidden).expect("decode");
    assert_eq!(decoded.validate(), Err(DiagnosticError::InvalidEvent));
}

// ---------- Bounded producer ----------

#[test]
fn producer_is_bounded_non_blocking_and_fifo() {
    let mut producer = DiagnosticProducer::new(2);
    let event = |name: &str| DiagnosticEvent::new(DataClass::Diagnostic, name, 0).expect("event");
    assert!(producer.enqueue(event("e.1")));
    assert!(producer.enqueue(event("e.2")));
    // Full queue: the incoming event is dropped, counted, never blocking.
    assert!(!producer.enqueue(event("e.3")));
    assert!(!producer.enqueue(event("e.4")));
    assert_eq!(producer.dropped(), 2);
    assert_eq!(producer.len(), 2);

    let drained: Vec<String> = producer.drain().map(|e| e.name().to_owned()).collect();
    assert_eq!(drained, vec!["e.1", "e.2"]);
    assert!(producer.is_empty());
    // Drops survive a drain; new events flow again.
    assert!(producer.enqueue(event("e.5")));
    assert_eq!(producer.dropped(), 2);
}

#[test]
fn producer_zero_capacity_converges_to_one() {
    let mut producer = DiagnosticProducer::new(0);
    let event = DiagnosticEvent::new(DataClass::Diagnostic, "e", 0).expect("event");
    assert!(producer.enqueue(event.clone()));
    assert!(!producer.enqueue(event));
    assert_eq!(producer.dropped(), 1);
    // The default capacity constant stays within the documented bound.
    assert_eq!(DEFAULT_QUEUE_CAPACITY, 256);
}
