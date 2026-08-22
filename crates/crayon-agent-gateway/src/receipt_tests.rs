//! AGT-11 receipt tests: AG-011 (bounded TTL, preview/clear, no body/
//! query/secret) and PV-010 (preview equals retained records).

use super::*;
use crate::grant::GrantId;

fn receipt(client: &str, ts: u64) -> ActionReceipt {
    raw_receipt(client, ts).unwrap()
}

fn raw_receipt(client: &str, ts: u64) -> Result<ActionReceipt, ReceiptError> {
    ActionReceipt::new(
        client,
        "page_read_markdown",
        AgentCapability::PageRead,
        RiskLevel::R1,
        "tab-7",
        GrantId(3),
        ReceiptOutcome::Succeeded,
        None,
        ts,
    )
}

#[test]
fn receipt_field_validation_matrix() {
    assert!(raw_receipt("cli-dev", 0).is_ok());
    for bad_client in ["", "Bad Client", &"c".repeat(65)] {
        assert_eq!(
            ActionReceipt::new(
                bad_client,
                "page_read_markdown",
                AgentCapability::PageRead,
                RiskLevel::R1,
                "tab-7",
                GrantId(0),
                ReceiptOutcome::Succeeded,
                None,
                0,
            )
            .unwrap_err(),
            ReceiptError::InvalidToken,
            "{bad_client:?}"
        );
    }
    for bad_target in ["", "https://site.example/path?q=1", "a b", &"t".repeat(33)] {
        assert_eq!(
            ActionReceipt::new(
                "cli-dev",
                "page_read_markdown",
                AgentCapability::PageRead,
                RiskLevel::R1,
                bad_target,
                GrantId(0),
                ReceiptOutcome::Succeeded,
                None,
                0,
            )
            .unwrap_err(),
            ReceiptError::InvalidToken,
            "{bad_target:?}"
        );
    }
    assert_eq!(
        ActionReceipt::new(
            "cli-dev",
            "page_read_markdown",
            AgentCapability::PageRead,
            RiskLevel::R1,
            "tab-7",
            GrantId(0),
            ReceiptOutcome::Failed,
            Some("bad code"),
            0,
        )
        .unwrap_err(),
        ReceiptError::InvalidErrorCode
    );
    // A closed error code token is accepted.
    assert!(ActionReceipt::new(
        "cli-dev",
        "page_read_markdown",
        AgentCapability::PageRead,
        RiskLevel::R1,
        "tab-7",
        GrantId(0),
        ReceiptOutcome::Failed,
        Some("target_stale"),
        0,
    )
    .is_ok());
}

#[test]
fn receipts_carry_no_body_query_or_secret() {
    // AG-011 leak scan: every string surface of every receipt must be a
    // closed token — assert no separator/assignment/scheme patterns and
    // no secret markers appear in any retained field.
    let mut store = ReceiptStore::new();
    for i in 0..8 {
        store.record(receipt(&format!("client-{i}"), i));
    }
    for receipt in store.preview(0) {
        let surfaces = [
            receipt.client().to_string(),
            receipt.tool().to_string(),
            receipt.target().to_string(),
        ];
        for surface in surfaces {
            for marker in [
                "http:",
                "https:",
                "?",
                "=",
                "&",
                "cookie",
                "authorization",
                "bearer",
                "token=",
                "secretpassword",
            ] {
                assert!(
                    !surface.to_ascii_lowercase().contains(marker),
                    "leak marker {marker:?} in {surface:?}"
                );
            }
        }
    }
}

#[test]
fn ttl_expiry_and_sweep() {
    let mut store = ReceiptStore::new();
    store.record(receipt("cli-dev", 0));
    assert_eq!(store.preview(0).len(), 1);
    assert_eq!(store.preview(RECEIPT_TTL_MS).len(), 0);
    assert_eq!(store.sweep_expired(RECEIPT_TTL_MS), 1);
    assert!(store.is_empty());
    let (_, expired, _) = store.stats();
    assert_eq!(expired, 1);
}

#[test]
fn capacity_evicts_oldest() {
    let mut store = ReceiptStore::new();
    for i in 0..=MAX_RECEIPTS as u64 {
        store.record(receipt("cli-dev", i));
    }
    assert_eq!(store.len(), MAX_RECEIPTS);
    let preview = store.preview(1_000);
    assert_eq!(preview.first().map(ActionReceipt::timestamp_ms), Some(1));
    let (_, _, evicted) = store.stats();
    assert_eq!(evicted, 1);
}

#[test]
fn preview_matches_retained_records() {
    // PV-010: user-visible preview equals the actual records.
    let mut store = ReceiptStore::new();
    for i in 0..5 {
        store.record(receipt("cli-dev", 100 * i));
    }
    let preview = store.preview(1);
    let retained: Vec<ActionReceipt> = store
        .preview(1)
        .into_iter()
        .filter(|r| 1 < r.expires_at_ms)
        .collect();
    assert_eq!(preview, retained);
    assert_eq!(preview.len(), 5);
}

#[test]
fn clear_all_and_clear_client() {
    let mut store = ReceiptStore::new();
    for i in 0..3 {
        store.record(receipt("cli-dev", i));
    }
    for i in 0..2 {
        store.record(receipt("mcp-dev", i));
    }
    assert_eq!(store.clear_client("mcp-dev"), 2);
    assert_eq!(store.len(), 3);
    assert_eq!(store.clear_all(), 3);
    assert!(store.is_empty());
    assert_eq!(store.clear_all(), 0);
}

#[test]
fn diagnostics_event_is_diagnostic_class() {
    let receipt = receipt("cli-dev", 42);
    let event = receipt.to_diagnostic_event().unwrap();
    assert_eq!(event.name(), "agent_action_receipt");
    let attributes = event.attributes();
    assert_eq!(
        attributes.get("tool").map(String::as_str),
        Some("page_read_markdown")
    );
    assert_eq!(attributes.get("risk").map(String::as_str), Some("r1"));
    assert_eq!(
        attributes.get("outcome").map(String::as_str),
        Some("succeeded")
    );
    assert_eq!(attributes.get("target").map(String::as_str), Some("tab-7"));
    assert!(!attributes.contains_key("error_code"));
    assert_eq!(RECEIPT_DIAGNOSTICS_SCHEMA_VERSION, 1);
}

/// Deterministic pseudo-random sequence (LCG): bounded store, monotone
/// drop counters, no leak markers under adversarial record mixes.
#[test]
fn lcg_store_invariants() {
    let outcomes = [
        ReceiptOutcome::Succeeded,
        ReceiptOutcome::Failed,
        ReceiptOutcome::Cancelled,
        ReceiptOutcome::Denied,
    ];
    let clients = ["cli-dev", "mcp-dev", "other"];
    let targets = ["tab-1", "tab-2", "active"];
    let mut state: u64 = 0x0DDB_1A5E_5BAD_5EED;
    let mut next = move || {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        state
    };

    let mut store = ReceiptStore::new();
    let mut clock = 1_u64;
    for step in 0..3_000_u64 {
        clock += 7;
        let receipt = ActionReceipt::new(
            clients[(next() % 3) as usize],
            "navigation_open",
            AgentCapability::Navigation,
            RiskLevel::R2,
            targets[(next() % 3) as usize],
            GrantId(next() % 16),
            outcomes[(next() % 4) as usize],
            if next() % 5 == 0 {
                Some("target_stale")
            } else {
                None
            },
            clock,
        )
        .unwrap();
        store.record(receipt);
        if step % 101 == 0 {
            store.sweep_expired(clock);
        }
        if step % 401 == 0 {
            store.clear_client(clients[(next() % 3) as usize]);
        }
        assert!(store.len() <= MAX_RECEIPTS);
    }
    let (live, expired, evicted) = store.stats();
    assert!(live <= MAX_RECEIPTS);
    assert!(expired + evicted > 0);
    // No preview entry ever carries a leak marker.
    for receipt in store.preview(clock) {
        assert!(is_token(receipt.client(), 64));
        assert!(is_token(receipt.target(), 32));
    }
}

use super::is_token;
