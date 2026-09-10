//! Concrete gateway dispatch (AGT-12Cb).
//!
//! Assembles the frozen AGT building blocks — `SessionManager`,
//! `GrantManager`, `ToolRegistry` and `ReceiptStore` — behind the AGT-12Ca
//! [`CaapDispatch`] trait. Actual tool execution is injected as [`ToolPort`];
//! the desktop host (AGT-12Cc) provides the real implementation backed by
//! the CEF shell.
//!
//! Default deny: a tool that is unknown, permanently denied, missing a
//! grant, or missing user confirmation (R2+ grants exist only after the
//! AGT-05 confirmation flow minted one) is rejected with a stable error
//! code, and every decision lands in the bounded receipt store.

use std::collections::BTreeMap;

use crayon_domain::{AgentCapability, AgentTarget, CaapError, RiskLevel, TabId};
use crayon_ipc_schema::{CaapChunk, MAX_CAAP_CHUNK_BYTES};
use crayon_ipc_schema::{CaapRequest, SchemaVersion};

use super::{CaapDispatch, CancelFlag, DispatchOutcome};
use crate::grant::{
    Authorization, GrantError, GrantId, GrantKind, GrantManager, GrantRequest, ProfileScope,
};
use crate::receipt::{ActionReceipt, ReceiptOutcome, ReceiptStore};
use crate::registry::{is_permanently_denied, ToolRegistry};
use crate::session::{SessionManager, SubmitOutcome};

/// Replay cache bound: idempotent retries re-serve the recorded response.
const REPLAY_CACHE_ENTRIES: usize = 32;

/// Tool execution surface injected by the host. Real implementations live
/// behind the CEF shell (AGT-12Cc); they must respect the request deadline
/// and poll the cancel flag at bounded checkpoints.
pub trait ToolPort {
    /// Resolves the request target to a concrete tab. `ActiveTab` is
    /// resolved by the host; unknown tabs fail closed.
    fn resolve_target(&mut self, target: &AgentTarget) -> Result<TabId, CaapError>;

    /// Executes one confirmed, authorized tool and returns the bounded
    /// response text (split into schema-valid chunks by the gateway).
    fn execute(
        &mut self,
        tool: &str,
        request: &CaapRequest,
        tab: &TabId,
        cancel: &CancelFlag,
    ) -> Result<String, CaapError>;
}

/// Host-invariant configuration.
pub struct GatewayConfig {
    /// Profile all grants and receipts are scoped to.
    pub profile: ProfileScope,
    /// Grant time-to-live for confirmations.
    pub grant_ttl_ms: u64,
}

/// Receipt input bundle: attribution (capability/risk), the grant that
/// authorized the call (zero id = none existed) and the terminal code.
struct RecordInput<'a> {
    capability: AgentCapability,
    risk: RiskLevel,
    grant: GrantId,
    outcome: ReceiptOutcome,
    code: Option<&'a str>,
}

/// The concrete AGT-12Cb dispatch. Owns session, grant, registry and
/// receipt state; tool execution is delegated to the injected port.
pub struct GatewayDispatch<P: ToolPort> {
    sessions: SessionManager,
    grants: GrantManager,
    registry: ToolRegistry,
    receipts: ReceiptStore,
    port: P,
    profile: ProfileScope,
    grant_ttl_ms: u64,
    clock: Box<dyn Fn() -> u64 + Send>,
    /// Bounded idempotent-replay cache: request id → response text.
    replay: BTreeMap<u64, String>,
}

impl<P: ToolPort> GatewayDispatch<P> {
    #[must_use]
    pub fn new(port: P, config: GatewayConfig, clock: Box<dyn Fn() -> u64 + Send>) -> Self {
        Self {
            sessions: SessionManager::new(),
            grants: GrantManager::new(),
            registry: ToolRegistry::with_v1_tools(),
            receipts: ReceiptStore::new(),
            port,
            profile: config.profile,
            grant_ttl_ms: config.grant_ttl_ms,
            clock,
            replay: BTreeMap::new(),
        }
    }

    /// Issues a grant for a user-confirmed scope (AGT-05 flow). The caller
    /// must have collected the confirmation; this method only mints.
    pub fn grant(
        &mut self,
        client: &str,
        capability: AgentCapability,
        target: Option<AgentTarget>,
        now_ms: u64,
    ) -> Result<(), CaapError> {
        self.grants
            .issue(
                GrantRequest {
                    kind: GrantKind::AppSession,
                    session: client.to_owned(),
                    profile: self.profile.clone(),
                    capability,
                    target,
                    task: None,
                    ttl_ms: self.grant_ttl_ms,
                },
                now_ms,
            )
            .map(|_| ())
            .map_err(|_| CaapError::Unauthorized)
    }

    /// Bounded receipt store access for diagnostics and user preview.
    #[must_use]
    pub const fn receipts(&self) -> &ReceiptStore {
        &self.receipts
    }

    fn record(&mut self, client: &str, request: &CaapRequest, input: RecordInput<'_>, now_ms: u64) {
        let target = target_token(request.target());
        if let Ok(receipt) = ActionReceipt::new(
            client,
            request.tool(),
            input.capability,
            input.risk,
            &target,
            input.grant,
            input.outcome,
            input.code,
            now_ms,
        ) {
            self.receipts.record(receipt);
        }
    }

    fn deny(
        &mut self,
        client: &str,
        request: &CaapRequest,
        spec: Option<(AgentCapability, RiskLevel)>,
        code: &'static str,
        now_ms: u64,
    ) -> DispatchOutcome {
        if let Some((capability, risk)) = spec {
            self.record(
                client,
                request,
                RecordInput {
                    capability,
                    risk,
                    grant: GrantId(0),
                    outcome: ReceiptOutcome::Denied,
                    code: Some(code),
                },
                now_ms,
            );
        }
        DispatchOutcome::Failed(match code {
            "tool_unknown" => CaapError::ToolUnknown,
            "target_invalid" => CaapError::TargetInvalid,
            _ => CaapError::CapabilityDenied,
        })
    }
}

/// Streams `text` as schema-valid chunks, drawing sequence numbers from
/// `seq_source`. Character boundaries are respected so no chunk carries a
/// partial UTF-8 sequence.
fn stream_chunks(
    request_id: u64,
    text: &str,
    mut next_seq: impl FnMut(bool) -> Result<u32, CaapError>,
    sink: &mut dyn FnMut(CaapChunk),
) -> DispatchOutcome {
    if text.is_empty() {
        let seq = match next_seq(true) {
            Ok(seq) => seq,
            Err(_) => return DispatchOutcome::Failed(CaapError::InvalidMessage),
        };
        if let Ok(chunk) = CaapChunk::new(request_id, seq, "", true) {
            sink(chunk);
        }
        return DispatchOutcome::Completed;
    }
    let total = chunk_count(text, MAX_CAAP_CHUNK_BYTES);
    let mut start = 0usize;
    for index in 0..total {
        let end = chunk_end(text, start, MAX_CAAP_CHUNK_BYTES);
        let is_final = index + 1 == total;
        let seq = match next_seq(is_final) {
            Ok(seq) => seq,
            Err(_) => return DispatchOutcome::Failed(CaapError::InvalidMessage),
        };
        match CaapChunk::new(request_id, seq, &text[start..end], is_final) {
            Ok(chunk) => sink(chunk),
            Err(_) => return DispatchOutcome::Failed(CaapError::InvalidMessage),
        }
        start = end;
    }
    DispatchOutcome::Completed
}

fn chunk_count(text: &str, max_bytes: usize) -> usize {
    let mut count = 0usize;
    let mut start = 0usize;
    while start < text.len() {
        start = chunk_end(text, start, max_bytes);
        count += 1;
    }
    count
}

fn chunk_end(text: &str, start: usize, max_bytes: usize) -> usize {
    let mut end = (start + max_bytes).min(text.len());
    while end > start && !text.is_char_boundary(end) {
        end -= 1;
    }
    end.max(start + 1).min(text.len())
}

fn target_token(target: &AgentTarget) -> String {
    match target {
        AgentTarget::ActiveTab => "active".to_owned(),
        AgentTarget::Tab { tab } => format!("tab-{}", tab.as_str()),
    }
}

fn error_code(error: CaapError) -> &'static str {
    match error {
        CaapError::VersionUnsupported => "version_unsupported",
        CaapError::CapabilityDenied => "capability_denied",
        CaapError::ToolUnknown => "tool_unknown",
        CaapError::TargetInvalid => "target_invalid",
        CaapError::TargetStale => "target_stale",
        CaapError::Cancelled => "cancelled",
        CaapError::DeadlineExceeded => "deadline_exceeded",
        CaapError::QueueFull => "queue_full",
        CaapError::Unauthorized => "unauthorized",
        CaapError::InvalidMessage => "invalid_message",
    }
}

impl<P: ToolPort> CaapDispatch for GatewayDispatch<P> {
    fn open_client(&mut self, client: &str, schema: SchemaVersion, _granted: &[AgentCapability]) {
        // A reconnect with the same name replaces the previous session and
        // revokes its grants.
        let _ = self.grants.revoke_session(client);
        let _ = self.sessions.close_session(client);
        let _ = self.sessions.open_session(client, schema);
    }

    fn close_client(&mut self, client: &str) {
        let _ = self.grants.revoke_session(client);
        let _ = self.sessions.close_session(client);
    }

    fn notify_cancel(&mut self, client: &str, request_id: u64) {
        // Only a queued/running task of this client can be cancelled; every
        // other id is an idempotent no-op inside the session manager.
        let _ = self.sessions.cancel(client, request_id);
    }

    fn dispatch(
        &mut self,
        client: &str,
        request: &CaapRequest,
        cancel: &CancelFlag,
        sink: &mut dyn FnMut(CaapChunk),
    ) -> DispatchOutcome {
        let now = (self.clock)();
        let spec = match self.registry.find(request.tool()) {
            Some(spec) if !is_permanently_denied(request.tool()) => spec,
            // Unknown and permanently denied tools are unattributable:
            // rejected without a receipt (no capability to record).
            _ => return DispatchOutcome::Failed(CaapError::ToolUnknown),
        };

        let tab = match self.port.resolve_target(request.target()) {
            Ok(tab) => tab,
            Err(_) => {
                return self.deny(
                    client,
                    request,
                    Some((spec.capability(), spec.risk())),
                    "target_invalid",
                    now,
                );
            }
        };

        match self.sessions.submit(client, request, &tab, now) {
            Ok(SubmitOutcome::Accepted) => {}
            Ok(SubmitOutcome::Duplicate { request_id, .. }) => {
                // Idempotent retry: re-serve the recorded response instead
                // of executing again. The recorded task is already
                // terminal, so sequence numbers restart locally.
                let mut seq: u32 = 0;
                let text = self.replay.get(&request_id).cloned();
                return match text {
                    Some(text) => stream_chunks(
                        request.id(),
                        &text,
                        |is_final| {
                            seq += 1;
                            let _ = is_final;
                            Ok(seq)
                        },
                        sink,
                    ),
                    None => DispatchOutcome::Failed(CaapError::InvalidMessage),
                };
            }
            Err(error) => {
                self.record(
                    client,
                    request,
                    RecordInput {
                        capability: spec.capability(),
                        risk: spec.risk(),
                        grant: GrantId(0),
                        outcome: ReceiptOutcome::Failed,
                        code: Some(error_code(error)),
                    },
                    now,
                );
                return DispatchOutcome::Failed(error);
            }
        }

        // Default deny: R2+ tools succeed here only after the user
        // confirmation minted a grant through `grant` (AGT-05 flow).
        let authorization: Authorization = match self.grants.authorize(
            client,
            &self.profile,
            spec.capability(),
            Some(request.target()),
            now,
        ) {
            Ok(authorization) => authorization,
            Err(GrantError::TargetStale) => {
                self.record(
                    client,
                    request,
                    RecordInput {
                        capability: spec.capability(),
                        risk: spec.risk(),
                        grant: GrantId(0),
                        outcome: ReceiptOutcome::Denied,
                        code: Some("target_stale"),
                    },
                    now,
                );
                let _ = self
                    .sessions
                    .fail(client, request.id(), CaapError::TargetStale);
                return DispatchOutcome::Failed(CaapError::TargetStale);
            }
            Err(_) => {
                self.record(
                    client,
                    request,
                    RecordInput {
                        capability: spec.capability(),
                        risk: spec.risk(),
                        grant: GrantId(0),
                        outcome: ReceiptOutcome::Denied,
                        code: Some("capability_denied"),
                    },
                    now,
                );
                let _ = self
                    .sessions
                    .fail(client, request.id(), CaapError::CapabilityDenied);
                return DispatchOutcome::Failed(CaapError::CapabilityDenied);
            }
        };

        if let Err(error) = self.sessions.start(client, request.id()) {
            self.record(
                client,
                request,
                RecordInput {
                    capability: spec.capability(),
                    risk: spec.risk(),
                    grant: authorization.grant,
                    outcome: ReceiptOutcome::Failed,
                    code: Some(error_code(error)),
                },
                now,
            );
            return DispatchOutcome::Failed(error);
        }

        match self.port.execute(request.tool(), request, &tab, cancel) {
            Ok(text) => {
                self.replay.insert(request.id(), text.clone());
                while self.replay.len() > REPLAY_CACHE_ENTRIES {
                    let oldest = *self.replay.keys().next().expect("non-empty");
                    self.replay.remove(&oldest);
                }
                let outcome = stream_chunks(
                    request.id(),
                    &text,
                    |is_final| self.sessions.next_chunk(client, request.id(), is_final),
                    sink,
                );
                let _ = self.sessions.complete(client, request.id());
                self.record(
                    client,
                    request,
                    RecordInput {
                        capability: spec.capability(),
                        risk: spec.risk(),
                        grant: authorization.grant,
                        outcome: ReceiptOutcome::Succeeded,
                        code: None,
                    },
                    now,
                );
                outcome
            }
            Err(CaapError::Cancelled) => {
                let _ = self.sessions.cancel(client, request.id());
                self.record(
                    client,
                    request,
                    RecordInput {
                        capability: spec.capability(),
                        risk: spec.risk(),
                        grant: authorization.grant,
                        outcome: ReceiptOutcome::Cancelled,
                        code: Some("cancelled"),
                    },
                    now,
                );
                DispatchOutcome::Failed(CaapError::Cancelled)
            }
            Err(error) => {
                let _ = self.sessions.fail(client, request.id(), error);
                self.record(
                    client,
                    request,
                    RecordInput {
                        capability: spec.capability(),
                        risk: spec.risk(),
                        grant: authorization.grant,
                        outcome: ReceiptOutcome::Failed,
                        code: Some(error_code(error)),
                    },
                    now,
                );
                DispatchOutcome::Failed(error)
            }
        }
    }
}
