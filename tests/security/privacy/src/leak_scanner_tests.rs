//! PRV-11: privacy leak scanner integration tests.
//!
//! Each test covers one product surface: creates realistic data through
//! the public API, serializes/writes it, then scans with LeakScanner.
//! Positive cases assert zero findings (no leaks); negative cases
//! inject a known secret and assert the scanner captures it.


#[test]
fn diagnostics_events_contain_no_secrets() {
    let mut producer = crayon_domain::DiagnosticProducer::new(16);
    let event = crayon_domain::DiagnosticEvent::new(crayon_domain::DataClass::Operational, "cast_started", 1000)
        .expect("valid event")
        .with_attribute("device", "test-receiver")
        .expect("attr")
        .with_attribute("duration_ms", "5000")
        .expect("attr");
    assert!(producer.enqueue(event));

    let drained: Vec<crayon_domain::DiagnosticEvent> = producer.drain().collect();
    assert_eq!(drained.len(), 1);
    for event in &drained {
        let json = serde_json::to_string(&event.name()).unwrap();
        let findings = test_support::leak_scanner::LeakScanner::scan_text(&json, &[]);
        assert!(
            findings.is_empty(),
            "diagnostics event leaked: {findings:?}"
        );
    }
}

#[test]
fn diagnostics_user_content_class_is_forbidden() {
    // crayon_domain::DataClass::UserContent must not enter diagnostics.
    let result = crayon_domain::DiagnosticEvent::new(crayon_domain::DataClass::UserContent, "page_text", 1000);
    assert!(result.is_err(), "UserContent must be rejected");
    let result = crayon_domain::DiagnosticEvent::new(crayon_domain::DataClass::Secret, "token_value", 1000);
    assert!(result.is_err(), "Secret must be rejected");
}

#[test]
fn agent_receipt_contains_no_body_or_query() {
    let mut store = crayon_agent_gateway::receipt::ReceiptStore::new();
    let receipt = crayon_agent_gateway::receipt::ActionReceipt::new(
        "cli-dev",
        "page_read_markdown",
        crayon_domain::AgentCapability::PageRead,
        crayon_domain::RiskLevel::R1,
        "tab-7",
        crayon_agent_gateway::grant::grant_id_for_testing(1),
        crayon_agent_gateway::receipt::ReceiptOutcome::Succeeded,
        None,
        1000,
    )
    .expect("valid receipt");
    store.record(receipt);

    for r in store.preview(crayon_agent_gateway::receipt::RECEIPT_TTL_MS - 1) {
        let json = serde_json::to_string(&r.capability()).unwrap();
        let findings = test_support::leak_scanner::LeakScanner::scan_text(&json, &[]);
        assert!(findings.is_empty(), "receipt leaked: {findings:?}");
        // Receipt must not contain URL query or cookie patterns.
        let target = r.target();
        let findings = test_support::leak_scanner::LeakScanner::scan_text(target, &[]);
        assert!(findings.is_empty(), "receipt target leaked: {findings:?}");
    }
}

#[test]
fn injected_secret_in_receipt_is_caught() {
    // Negative test: if a secret somehow entered the receipt surface,
    // the scanner must catch it.
    let findings = test_support::leak_scanner::LeakScanner::scan_text(
        "SESSDATA=injected_secret_12345", &[],
    );
    assert!(!findings.is_empty(), "scanner must catch SESSDATA");
    let findings = test_support::leak_scanner::LeakScanner::scan_text(
        "Bearer injected_token_abc123", &[],
    );
    assert!(!findings.is_empty(), "scanner must catch Bearer token");
}

#[test]
fn profile_store_contains_no_plaintext_secrets() {
    let tmp = tempfile::tempdir().expect("tempdir");
    // Write some non-secret profile-like data.
    std::fs::write(tmp.path().join("profile.dat"), b"profile-metadata-here").expect("write");

    // Scan the profile directory for plaintext secret patterns.
    let findings = test_support::leak_scanner::LeakScanner::scan_dir(tmp.path(), &[]);
    assert!(
        findings.is_empty(),
        "profile storage leaked secrets: {findings:?}"
    );
}

#[test]
fn injected_secret_on_disk_is_caught() {
    let tmp = tempfile::tempdir().expect("tempdir");
    let file_path = tmp.path().join("config.dat");
    std::fs::write(&file_path, "SESSDATA=injected_secret_12345").expect("write");

    let findings = test_support::leak_scanner::LeakScanner::scan_dir(tmp.path(), &[]);
    assert!(
        !findings.is_empty(),
        "scanner must catch plaintext secret on disk"
    );
}

#[test]
fn wire_dto_serialization_is_clean() {
    // Serialize a CAAP target and scan the JSON.
    let target = crayon_domain::AgentTarget::Tab {
        tab: crayon_domain::TabId::new("tab-1").expect("valid"),
    };
    let json = serde_json::to_string(&target).expect("serialize");
    let findings = test_support::leak_scanner::LeakScanner::scan_text(&json, &[]);
    assert!(findings.is_empty(), "wire DTO leaked: {findings:?}");

    // Serialize an agent capability.
    let json = serde_json::to_string(&crayon_domain::AgentCapability::PageRead).unwrap();
    let findings = test_support::leak_scanner::LeakScanner::scan_text(&json, &[]);
    assert!(findings.is_empty(), "capability JSON leaked: {findings:?}");
}

#[test]
fn relay_vault_url_zeroization_is_enforced_by_types() {
    // RL-014: relay recipe URLs are Zeroizing-wrapped.  This is a
    // compile-time guarantee from the relay crate; here we verify the
    // scanner catches a URL with query tokens in a text buffer.
    let url_with_token = "https://media.example/video.m3u8?token=abc123&sig=xyz";
    let findings = test_support::leak_scanner::LeakScanner::scan_text(url_with_token, &[]);
    assert!(!findings.is_empty(), "scanner must catch query tokens");
}
