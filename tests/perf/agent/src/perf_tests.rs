use crayon_agent_gateway::tools::semantic::{
    invoke_caap, SemanticActionPort, SemanticInvokeRequest, SemanticRejection,
    MAX_SEMANTIC_ARGUMENT_BYTES, SEMANTIC_INVOKE_TOOL,
};
use crayon_domain::{
    ActionKind, AgentTarget, EffectOutcome, EffectReport, SemanticNodeId, SessionGeneration, TabId,
};
use crayon_ipc_schema::CaapRequest;
use crayon_semantic_action::ActionHandleId;
use std::collections::BTreeMap;

fn request(sequence: u64, argument: &str) -> CaapRequest {
    let id = ActionHandleId::generate().expect("handle");
    CaapRequest::new(
        sequence,
        SEMANTIC_INVOKE_TOOL,
        AgentTarget::ActiveTab,
        10_000,
        &format!("idem-{sequence}"),
        BTreeMap::from([
            ("action_id".to_owned(), id.as_str().to_owned()),
            ("args".to_owned(), argument.to_owned()),
        ]),
    )
    .expect("request")
}

struct Port(usize);

impl SemanticActionPort for Port {
    fn invoke(
        &mut self,
        _request: &SemanticInvokeRequest,
    ) -> Result<EffectReport, SemanticRejection> {
        self.0 += 1;
        EffectReport::new(
            TabId::new("tab-1").expect("tab"),
            SessionGeneration::from_raw(1),
            1,
            ActionKind::SetText,
            SemanticNodeId::new("node-1").expect("node"),
            EffectOutcome::Verified,
            None,
            None,
        )
        .map_err(|_| SemanticRejection::QueueFull)
    }
}

#[test]
fn maximum_argument_produces_one_bounded_final_chunk() {
    let mut port = Port(0);
    let chunk = invoke_caap(
        &mut port,
        &request(1, &"x".repeat(MAX_SEMANTIC_ARGUMENT_BYTES)),
    )
    .expect("bounded chunk");
    let wire = serde_json::to_string(&chunk).expect("wire");
    assert_eq!(port.0, 1);
    assert!(wire.len() <= 4096);
}

#[test]
fn repeated_invocations_have_linear_bounded_dispatch_count() {
    const INVOCATIONS: u64 = 1_024;
    let mut port = Port(0);
    for sequence in 1..=INVOCATIONS {
        invoke_caap(&mut port, &request(sequence, "bounded input")).expect("effect");
    }
    assert_eq!(port.0, INVOCATIONS as usize);
}
