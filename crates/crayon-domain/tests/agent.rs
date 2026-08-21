//! CAAP v1 domain primitive tests (AGT-01): capability closure and
//! forbidden-capability non-expressibility, risk mapping, target wire
//! form and the stable error-code set (AG-001).

use crayon_domain::{AgentCapability, AgentTarget, CaapError, RiskLevel, TabId};

// ---------- Capability closure ----------

#[test]
fn capability_set_is_closed_and_complete() {
    assert_eq!(AgentCapability::ALL.len(), 5);
    // The wire names are the compatibility contract.
    let wire: Vec<String> = AgentCapability::ALL
        .iter()
        .map(|cap| serde_json::to_string(cap).expect("serialize"))
        .collect();
    assert_eq!(
        wire,
        vec![
            "\"page_read\"",
            "\"navigation\"",
            "\"cast_read\"",
            "\"cast_control\"",
            "\"semantic_action\""
        ]
    );
}

#[test]
fn forbidden_capabilities_are_not_expressible() {
    // Permanently forbidden surfaces must not deserialize as capabilities.
    for forbidden in [
        "\"raw_cdp\"",
        "\"webdriver\"",
        "\"execute_javascript\"",
        "\"arbitrary_javascript\"",
        "\"cookies\"",
        "\"credentials\"",
        "\"cookie_read\"",
        "\"password\"",
        "\"payment\"",
        "\"file_upload\"",
        "\"file_system\"",
        "\"network\"",
        "\"proxy\"",
    ] {
        assert!(
            serde_json::from_str::<AgentCapability>(forbidden).is_err(),
            "forbidden capability {forbidden} must not deserialize"
        );
    }
}

#[test]
fn capability_risk_mapping_is_closed() {
    assert_eq!(AgentCapability::CastRead.risk_level(), RiskLevel::R0);
    assert_eq!(AgentCapability::PageRead.risk_level(), RiskLevel::R1);
    assert_eq!(AgentCapability::Navigation.risk_level(), RiskLevel::R2);
    assert_eq!(AgentCapability::CastControl.risk_level(), RiskLevel::R3);
    assert_eq!(AgentCapability::SemanticAction.risk_level(), RiskLevel::R4);
    // Risk levels serialize as r0..r4 and reject anything else.
    assert_eq!(
        serde_json::to_string(&RiskLevel::R0).expect("ser"),
        "\"r0\""
    );
    assert_eq!(
        serde_json::to_string(&RiskLevel::R4).expect("ser"),
        "\"r4\""
    );
    assert!(serde_json::from_str::<RiskLevel>("\"r5\"").is_err());
}

// ---------- Targets ----------

#[test]
fn target_wire_form_is_closed() {
    let target = AgentTarget::Tab {
        tab: TabId::new("tab-1").expect("tab id"),
    };
    let json = serde_json::to_value(&target).expect("serialize");
    assert_eq!(json, serde_json::json!({"kind": "tab", "tab": "tab-1"}));
    let decoded: AgentTarget = serde_json::from_value(json).expect("decode");
    assert_eq!(decoded, target);

    let active = AgentTarget::ActiveTab;
    assert_eq!(
        serde_json::to_value(&active).expect("serialize"),
        serde_json::json!({"kind": "active_tab"})
    );

    // Unknown target kinds and invalid tab ids are rejected.
    assert!(serde_json::from_value::<AgentTarget>(serde_json::json!({"kind": "window"})).is_err());
    assert!(serde_json::from_value::<AgentTarget>(
        serde_json::json!({"kind": "tab", "tab": "bad tab!"})
    )
    .is_err());
}

// ---------- Stable error codes ----------

#[test]
fn error_code_set_is_locked() {
    let wire: Vec<String> = CaapError::ALL
        .iter()
        .map(|error| serde_json::to_string(error).expect("serialize"))
        .collect();
    assert_eq!(
        wire,
        vec![
            "\"version_unsupported\"",
            "\"capability_denied\"",
            "\"tool_unknown\"",
            "\"target_invalid\"",
            "\"target_stale\"",
            "\"cancelled\"",
            "\"deadline_exceeded\"",
            "\"queue_full\"",
            "\"unauthorized\"",
            "\"invalid_message\""
        ]
    );
    // Unknown codes never deserialize.
    assert!(serde_json::from_str::<CaapError>("\"internal_error\"").is_err());
    // Codes roundtrip through Display-free wire form only.
    for error in CaapError::ALL {
        let json = serde_json::to_string(&error).expect("serialize");
        let decoded: CaapError = serde_json::from_str(&json).expect("decode");
        assert_eq!(decoded, error);
    }
}
