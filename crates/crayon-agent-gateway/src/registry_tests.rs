//! AGT-02 registry behavior tests: frozen v1 snapshot, risk/confirmation
//! derivation, registration rejection matrix and the permanent deny list.

use super::*;
use crayon_domain::AgentCapability;

const V1_TOOL_COUNT: usize = 20;

fn golden_path() -> std::path::PathBuf {
    std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests")
        .join("agent_registry_v1_snapshot.txt")
}

fn read_tool() -> ToolSpec {
    ToolSpec::build("page.snapshot", AgentCapability::PageRead, true, true, &[])
}

#[test]
fn v1_registry_matches_frozen_snapshot_golden() {
    let registry = ToolRegistry::with_v1_tools();
    assert_eq!(registry.len(), V1_TOOL_COUNT);
    let golden = std::fs::read_to_string(golden_path()).expect("snapshot golden must exist");
    let actual = registry.snapshot();
    let golden_lines: Vec<&str> = golden.lines().collect();
    let actual_lines: Vec<&str> = actual.lines().collect();
    assert_eq!(actual_lines.len(), V1_TOOL_COUNT);
    for (index, (expected, got)) in golden_lines.iter().zip(actual_lines.iter()).enumerate() {
        assert_eq!(got, expected, "snapshot line {index} diverged from golden");
    }
    assert_eq!(actual, golden);
}

#[test]
fn every_tool_risk_matches_capability_risk_level() {
    let registry = ToolRegistry::with_v1_tools();
    for spec in registry.iter() {
        assert_eq!(
            spec.risk(),
            spec.capability().risk_level(),
            "tool {} risk must match capability risk",
            spec.name()
        );
    }
}

#[test]
fn confirmation_and_availability_derive_from_risk() {
    let cases = [
        (
            AgentCapability::CastRead,
            ConfirmationRequirement::None,
            Availability::Enabled,
        ),
        (
            AgentCapability::PageRead,
            ConfirmationRequirement::None,
            Availability::Enabled,
        ),
        (
            AgentCapability::Navigation,
            ConfirmationRequirement::Required,
            Availability::Enabled,
        ),
        (
            AgentCapability::CastControl,
            ConfirmationRequirement::Required,
            Availability::Enabled,
        ),
        (
            AgentCapability::SemanticAction,
            ConfirmationRequirement::Required,
            Availability::PreviewGated,
        ),
    ];
    for (capability, confirmation, availability) in cases {
        let spec = ToolSpec::build("probe.tool", capability, false, false, &[]);
        assert_eq!(spec.risk(), capability.risk_level());
        assert_eq!(spec.confirmation(), confirmation, "{capability:?}");
        assert_eq!(spec.availability(), availability, "{capability:?}");
    }
}

#[test]
fn register_rejects_duplicate_name() {
    let mut registry = ToolRegistry::new();
    registry.register(read_tool()).expect("first register");
    assert_eq!(
        registry.register(read_tool()),
        Err(RegistryError::DuplicateTool)
    );
}

#[test]
fn register_rejects_invalid_names() {
    let mut registry = ToolRegistry::new();
    let overlong = "a".repeat(MAX_TOOL_NAME_LEN + 1);
    for name in [
        "",
        "Page.Snapshot",
        "page snapshot",
        "页面.快照",
        overlong.as_str(),
    ] {
        let spec = ToolSpec::build(name, AgentCapability::PageRead, true, false, &[]);
        assert_eq!(
            registry.register(spec),
            Err(RegistryError::InvalidName),
            "name {name:?} must be rejected"
        );
    }
    // Boundary: exactly MAX_TOOL_NAME_LEN bytes is accepted.
    let exact = "a".repeat(MAX_TOOL_NAME_LEN);
    let spec = ToolSpec::build(&exact, AgentCapability::PageRead, true, false, &[]);
    assert_eq!(registry.register(spec), Ok(()));
}

#[test]
fn permanent_deny_list_hits_are_rejected() {
    let mut registry = ToolRegistry::new();
    let denied = [
        "cdp.send",
        "debug.webdriver_session",
        "page.execute_js",
        "browser.eval",
        "page.run_javascript",
        "page.read_cookies", // contains "cookie": denied even as a read
        "vault.read_credential",
        "form.fill_password",
        "checkout.submit_payment",
        "fs.file_upload_send",
        "local.file_system_list",
        "host.filesystem_read",
        "lan.network_scan",
        "net.proxy_set",
        "page.screenshot_capture",
    ];
    for name in denied {
        assert!(is_permanently_denied(name), "{name} must hit deny list");
        let spec = ToolSpec::build(name, AgentCapability::PageRead, true, false, &[]);
        assert_eq!(
            registry.register(spec),
            Err(RegistryError::PermanentlyDenied),
            "name {name} must be rejected"
        );
    }
    assert_eq!(registry.len(), 0);
}

#[test]
fn lookalike_names_without_denied_tokens_pass() {
    let mut registry = ToolRegistry::new();
    for name in [
        "page.snapshot",
        "page.markdown",
        "nav.navigate",
        "cast.get_state",
    ] {
        assert!(
            !is_permanently_denied(name),
            "{name} must not hit deny list"
        );
        let spec = ToolSpec::build(name, AgentCapability::PageRead, true, false, &[]);
        assert_eq!(registry.register(spec), Ok(()), "name {name} must register");
    }
}

#[test]
fn register_rejects_capacity_overflow() {
    let mut registry = ToolRegistry::new();
    for index in 0..MAX_TOOLS {
        let name = format!("probe.tool_{index:02}");
        let spec = ToolSpec::build(&name, AgentCapability::PageRead, true, false, &[]);
        registry.register(spec).expect("within capacity");
    }
    let spec = ToolSpec::build(
        "probe.overflow",
        AgentCapability::PageRead,
        true,
        false,
        &[],
    );
    assert_eq!(registry.register(spec), Err(RegistryError::Capacity));
    assert_eq!(registry.len(), MAX_TOOLS);
}

#[test]
fn register_rejects_param_shape_violations() {
    let mut registry = ToolRegistry::new();
    let too_many: Vec<(&str, bool)> = (0..MAX_PARAMS_PER_TOOL + 1).map(|_| ("p", false)).collect();
    let spec = ToolSpec::build(
        "probe.too_many",
        AgentCapability::PageRead,
        true,
        false,
        &too_many,
    );
    assert_eq!(registry.register(spec), Err(RegistryError::TooManyParams));

    for key in ["", "Key", "bad key"] {
        let spec = ToolSpec::build(
            "probe.bad_key",
            AgentCapability::PageRead,
            true,
            false,
            &[(key, true)],
        );
        assert_eq!(
            registry.register(spec),
            Err(RegistryError::InvalidParamKey),
            "param key {key:?} must be rejected"
        );
    }
}

#[test]
fn find_returns_none_for_unknown_tool() {
    let registry = ToolRegistry::with_v1_tools();
    assert!(registry.find("page.snapshot").is_some());
    assert_eq!(registry.find("page.unknown"), None);
    assert_eq!(registry.find(""), None);
}

#[test]
fn iter_yields_tools_in_name_order() {
    let registry = ToolRegistry::with_v1_tools();
    let names: Vec<&str> = registry.iter().map(ToolSpec::name).collect();
    let mut sorted = names.clone();
    sorted.sort_unstable();
    assert_eq!(names, sorted, "iteration order must be deterministic");
}

#[test]
fn param_spec_snapshot_marks_required_and_optional() {
    let registry = ToolRegistry::with_v1_tools();
    let spec = registry.find("page.snapshot").expect("frozen tool");
    let params = spec.params();
    assert_eq!(params.len(), 2);
    assert!(!params[0].required);
    assert_eq!(params[0].key, "format");
    assert!(!params[1].required);
    assert_eq!(params[1].key, "max_bytes");

    let invoke = registry.find("act.invoke").expect("frozen tool");
    assert!(invoke.params()[0].required);
    assert!(!invoke.params()[1].required);
    assert_eq!(invoke.availability(), Availability::PreviewGated);
}
