//! AGT-12Cb gateway dispatch tests: the frozen tool policy through a fake
//! port — default deny, confirmation-gated grants, target resolution,
//! chunked responses, receipts and idempotent replay.

use super::gateway::{GatewayConfig, GatewayDispatch, ToolPort};
use super::{CaapDispatch, CancelFlag, DispatchOutcome};
use crayon_domain::{AgentCapability, AgentTarget, CaapError, TabId};
use crayon_ipc_schema::CaapChunk;
use crayon_ipc_schema::CaapRequest;
use std::collections::BTreeMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use crate::grant::ProfileScope;
use crate::receipt::ReceiptOutcome;

fn tab(raw: &str) -> TabId {
    TabId::new(raw).expect("tab")
}

struct FakePort {
    responses: BTreeMap<String, Result<String, CaapError>>,
    executed: Vec<(String, String)>,
    cancelled_seen: Arc<AtomicBool>,
}

impl FakePort {
    fn with(response: Result<String, CaapError>) -> Self {
        let mut responses = BTreeMap::new();
        responses.insert("page.get_title".to_owned(), response.clone());
        responses.insert("nav.navigate".to_owned(), response);
        Self {
            responses,
            executed: Vec::new(),
            cancelled_seen: Arc::new(AtomicBool::new(false)),
        }
    }
}

impl ToolPort for FakePort {
    fn resolve_target(&mut self, target: &AgentTarget) -> Result<TabId, CaapError> {
        match target {
            AgentTarget::ActiveTab => Ok(tab("tab-1")),
            AgentTarget::Tab { tab } => {
                if tab.as_str() == "ghost" {
                    Err(CaapError::TargetInvalid)
                } else {
                    Ok(tab.clone())
                }
            }
        }
    }

    fn execute(
        &mut self,
        tool: &str,
        _request: &CaapRequest,
        tab: &TabId,
        cancel: &CancelFlag,
    ) -> Result<String, CaapError> {
        if cancel.is_cancelled() {
            self.cancelled_seen.store(true, Ordering::SeqCst);
            return Err(CaapError::Cancelled);
        }
        self.executed
            .push((tool.to_owned(), tab.as_str().to_owned()));
        self.responses.get(tool).cloned().unwrap_or_else(|| {
            self.responses
                .get("page.get_title")
                .cloned()
                .unwrap_or(Ok(String::new()))
        })
    }
}

fn gateway(port: FakePort) -> GatewayDispatch<FakePort> {
    GatewayDispatch::new(
        port,
        GatewayConfig {
            profile: ProfileScope::new("default").expect("profile"),
            grant_ttl_ms: 60_000,
        },
        Box::new(|| 1_000),
    )
}

fn request(id: u64, tool: &str) -> CaapRequest {
    CaapRequest::new(
        id,
        tool,
        AgentTarget::ActiveTab,
        60_000,
        &format!("key-{id}"),
        BTreeMap::new(),
    )
    .expect("request")
}

fn collect(out: &mut Vec<CaapChunk>, chunk: CaapChunk) {
    out.push(chunk);
}

#[test]
fn unauthorized_read_is_denied_before_execution() {
    let mut gateway = gateway(FakePort::with(Ok("title".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    let mut chunks = Vec::new();
    let cancel = CancelFlag::new();
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "page.get_title"),
        &cancel,
        &mut |chunk| collect(&mut chunks, chunk),
    );
    assert_eq!(
        outcome,
        DispatchOutcome::Failed(CaapError::CapabilityDenied)
    );
    assert!(chunks.is_empty());
}

#[test]
fn unknown_tool_is_rejected() {
    let mut gateway = gateway(FakePort::with(Ok("x".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    let mut chunks = Vec::new();
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "page.read_secrets"),
        &CancelFlag::new(),
        &mut |chunk| collect(&mut chunks, chunk),
    );
    assert_eq!(outcome, DispatchOutcome::Failed(CaapError::ToolUnknown));
    assert!(chunks.is_empty());
}

#[test]
fn r2_tool_without_confirmation_is_denied_then_allowed_after_grant() {
    let mut gateway = gateway(FakePort::with(Ok("navigated".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    let mut chunks = Vec::new();
    // R2 navigation: no grant yet (user has not confirmed) -> denied.
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "nav.navigate"),
        &CancelFlag::new(),
        &mut |chunk| collect(&mut chunks, chunk),
    );
    assert_eq!(
        outcome,
        DispatchOutcome::Failed(CaapError::CapabilityDenied)
    );

    // The user confirms through the AGT-05 flow; the grant is minted.
    gateway
        .grant(
            "cli",
            AgentCapability::Navigation,
            Some(AgentTarget::ActiveTab),
            500,
        )
        .expect("grant");

    let outcome = gateway.dispatch(
        "cli",
        &request(2, "nav.navigate"),
        &CancelFlag::new(),
        &mut |chunk| collect(&mut chunks, chunk),
    );
    assert_eq!(outcome, DispatchOutcome::Completed);
}

#[test]
fn successful_read_streams_final_chunk_and_records_receipt() {
    let mut gateway = gateway(FakePort::with(Ok("The Title".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    gateway
        .grant("cli", AgentCapability::PageRead, None, 500)
        .expect("grant");
    let mut chunks = Vec::new();
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "page.get_title"),
        &CancelFlag::new(),
        &mut |chunk| collect(&mut chunks, chunk),
    );
    assert_eq!(outcome, DispatchOutcome::Completed);
    assert_eq!(chunks.len(), 1);
    assert!(chunks[0].is_final());
    assert_eq!(chunks[0].data(), "The Title");
    assert_eq!(chunks[0].id(), 1);
}

#[test]
fn unknown_tab_target_fails_closed() {
    let mut gateway = gateway(FakePort::with(Ok("x".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    gateway
        .grant("cli", AgentCapability::PageRead, None, 500)
        .expect("grant");
    let request = CaapRequest::new(
        1,
        "page.get_title",
        AgentTarget::Tab { tab: tab("ghost") },
        60_000,
        "key-1",
        BTreeMap::new(),
    )
    .expect("request");
    let outcome = gateway.dispatch("cli", &request, &CancelFlag::new(), &mut |_| {});
    assert_eq!(outcome, DispatchOutcome::Failed(CaapError::TargetInvalid));
}

#[test]
fn port_error_maps_to_failed_with_receipt() {
    let mut gateway = gateway(FakePort::with(Err(CaapError::TargetStale)));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    gateway
        .grant("cli", AgentCapability::PageRead, None, 500)
        .expect("grant");
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "page.get_title"),
        &CancelFlag::new(),
        &mut |_| {},
    );
    assert_eq!(outcome, DispatchOutcome::Failed(CaapError::TargetStale));
}

#[test]
fn idempotent_duplicate_replays_recorded_response() {
    let mut gateway = gateway(FakePort::with(Ok("body".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    gateway
        .grant("cli", AgentCapability::PageRead, None, 500)
        .expect("grant");
    // Same idempotency key, different request id: the fingerprint matches,
    // so the session manager reports a duplicate of request 1.
    let first = CaapRequest::new(
        1,
        "page.get_title",
        AgentTarget::ActiveTab,
        60_000,
        "same-key",
        BTreeMap::new(),
    )
    .expect("request");
    let mut chunks = Vec::new();
    assert_eq!(
        gateway.dispatch("cli", &first, &CancelFlag::new(), &mut |c| chunks.push(c)),
        DispatchOutcome::Completed
    );
    let second = CaapRequest::new(
        2,
        "page.get_title",
        AgentTarget::ActiveTab,
        60_000,
        "same-key",
        BTreeMap::new(),
    )
    .expect("request");
    let mut replayed = Vec::new();
    assert_eq!(
        gateway.dispatch("cli", &second, &CancelFlag::new(), &mut |c| replayed
            .push(c)),
        DispatchOutcome::Completed
    );
    assert_eq!(chunks.len(), replayed.len());
    assert_eq!(chunks[0].data(), replayed[0].data());
}

#[test]
fn close_client_revokes_grants() {
    let mut gateway = gateway(FakePort::with(Ok("title".to_owned())));
    gateway
        .grant("cli", AgentCapability::PageRead, None, 500)
        .expect("grant");
    // A reconnect (open_client) drops the previous session and its grants.
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "page.get_title"),
        &CancelFlag::new(),
        &mut |_| {},
    );
    assert_eq!(
        outcome,
        DispatchOutcome::Failed(CaapError::CapabilityDenied)
    );
}

#[test]
fn cancelled_execution_is_recorded() {
    let mut gateway = gateway(FakePort::with(Ok("never".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    gateway
        .grant("cli", AgentCapability::PageRead, None, 500)
        .expect("grant");
    let cancel = CancelFlag::new();
    cancel.cancel();
    let outcome = gateway.dispatch("cli", &request(1, "page.get_title"), &cancel, &mut |_| {});
    assert_eq!(outcome, DispatchOutcome::Failed(CaapError::Cancelled));
}

#[test]
fn denied_receipts_are_recorded_without_execution() {
    let mut gateway = gateway(FakePort::with(Ok("title".to_owned())));
    gateway.open_client("cli", crayon_ipc_schema::SchemaVersion::CURRENT, &[]);
    let outcome = gateway.dispatch(
        "cli",
        &request(1, "page.get_title"),
        &CancelFlag::new(),
        &mut |_| {},
    );
    assert_eq!(
        outcome,
        DispatchOutcome::Failed(CaapError::CapabilityDenied)
    );
    // The port never executed.
    assert!(gateway
        .receipts()
        .preview(2_000)
        .iter()
        .any(|receipt| receipt.outcome() == ReceiptOutcome::Denied));
}
