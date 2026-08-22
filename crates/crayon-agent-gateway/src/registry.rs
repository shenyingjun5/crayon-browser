//! Tool/capability/risk registry (AGT-02) and the permanent deny list.
//!
//! The registry is declarative: it freezes the v1 tool set, derives
//! confirmation requirements from risk levels, and rejects anything on
//! the permanent deny list.  Scheduling, grants and sessions belong to
//! AGT-03/04.
//!
//! Permanently denied surfaces (raw CDP/WebDriver, arbitrary JavaScript,
//! cookies/credentials, password/payment, file upload, arbitrary
//! file-system or network access) cannot be registered at all.

use crayon_domain::{AgentCapability, RiskLevel};
use std::collections::BTreeMap;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum number of registered tools.
pub const MAX_TOOLS: usize = 64;

/// Maximum length of a tool name, in bytes.
pub const MAX_TOOL_NAME_LEN: usize = 64;

/// Maximum number of parameters on one tool.
pub const MAX_PARAMS_PER_TOOL: usize = 16;

/// Maximum length of a parameter key, in bytes.
pub const MAX_PARAM_KEY_LEN: usize = 32;

/// Permanently denied tool name patterns (substring match on the closed
/// token form).  Hitting any of these is a stable registration rejection,
/// regardless of the declared capability.
pub const PERMANENTLY_DENIED: &[&str] = &[
    "cdp",
    "webdriver",
    "execute_js",
    "eval",
    "javascript",
    "cookie",
    "credential",
    "password",
    "payment",
    "file_upload",
    "file_system",
    "filesystem",
    "network",
    "proxy",
    "screenshot_capture",
];

/// Reports whether a tool name hits the permanent deny list.
#[must_use]
pub fn is_permanently_denied(name: &str) -> bool {
    PERMANENTLY_DENIED
        .iter()
        .any(|denied| name.contains(denied))
}

/// Confirmation requirement derived from a tool's risk level.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ConfirmationRequirement {
    /// R0/R1: no confirmation.
    None,
    /// R2/R3: explicit user confirmation per grant.
    Required,
}

impl ConfirmationRequirement {
    /// Stable wire name used by the registry snapshot.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::None => "none",
            Self::Required => "required",
        }
    }
}

/// Availability gate of a tool.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Availability {
    /// Delivered in the current preview wave.
    Enabled,
    /// R4 tools: only usable after the dedicated security review GO.
    PreviewGated,
}

impl Availability {
    /// Stable wire name used by the registry snapshot.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Enabled => "enabled",
            Self::PreviewGated => "preview_gated",
        }
    }
}

/// One closed parameter declaration.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ParamSpec {
    pub key: String,
    pub required: bool,
}

/// One registered tool: a closed, declarative spec.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ToolSpec {
    name: String,
    capability: AgentCapability,
    risk: RiskLevel,
    confirmation: ConfirmationRequirement,
    availability: Availability,
    idempotent: bool,
    streaming: bool,
    params: Vec<ParamSpec>,
}

impl ToolSpec {
    /// Builds a spec; `confirmation` is derived from `risk` and
    /// `availability` from the R4 gate, so contradictory declarations are
    /// impossible by construction.
    fn build(
        name: &str,
        capability: AgentCapability,
        idempotent: bool,
        streaming: bool,
        params: &[(&str, bool)],
    ) -> Self {
        let risk = capability.risk_level();
        let confirmation = match risk {
            RiskLevel::R0 | RiskLevel::R1 => ConfirmationRequirement::None,
            RiskLevel::R2 | RiskLevel::R3 | RiskLevel::R4 => ConfirmationRequirement::Required,
        };
        let availability = match risk {
            RiskLevel::R4 => Availability::PreviewGated,
            RiskLevel::R0 | RiskLevel::R1 | RiskLevel::R2 | RiskLevel::R3 => Availability::Enabled,
        };
        Self {
            name: name.to_owned(),
            capability,
            risk,
            confirmation,
            availability,
            idempotent,
            streaming,
            params: params
                .iter()
                .map(|(key, required)| ParamSpec {
                    key: (*key).to_owned(),
                    required: *required,
                })
                .collect(),
        }
    }

    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    #[must_use]
    pub const fn capability(&self) -> AgentCapability {
        self.capability
    }

    #[must_use]
    pub const fn risk(&self) -> RiskLevel {
        self.risk
    }

    #[must_use]
    pub const fn confirmation(&self) -> ConfirmationRequirement {
        self.confirmation
    }

    #[must_use]
    pub const fn availability(&self) -> Availability {
        self.availability
    }

    #[must_use]
    pub const fn idempotent(&self) -> bool {
        self.idempotent
    }

    #[must_use]
    pub const fn streaming(&self) -> bool {
        self.streaming
    }

    #[must_use]
    pub fn params(&self) -> &[ParamSpec] {
        &self.params
    }
}

/// Registry operation failure.  Variants are stable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RegistryError {
    /// The tool name is empty, overlong or outside the token charset.
    InvalidName,
    /// A tool with this name is already registered.
    DuplicateTool,
    /// The name hits the permanent deny list.
    PermanentlyDenied,
    /// A parameter key violates shape or bounds.
    InvalidParamKey,
    /// The tool declares too many parameters.
    TooManyParams,
    /// The registry is full.
    Capacity,
    /// The declared risk contradicts the capability's risk level.
    RiskMismatch,
}

impl Display for RegistryError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidName => "tool name violates shape or bounds",
            Self::DuplicateTool => "tool is already registered",
            Self::PermanentlyDenied => "tool name hits the permanent deny list",
            Self::InvalidParamKey => "parameter key violates shape or bounds",
            Self::TooManyParams => "tool declares too many parameters",
            Self::Capacity => "tool registry capacity reached",
            Self::RiskMismatch => "declared risk contradicts the capability risk level",
        };
        formatter.write_str(message)
    }
}

impl Error for RegistryError {}

/// Reports whether `name` uses the closed token charset `[a-z0-9_.:-]`
/// within `max_len`.
pub(crate) fn is_token(name: &str, max_len: usize) -> bool {
    !name.is_empty()
        && name.len() <= max_len
        && name.bytes().all(|byte| {
            byte.is_ascii_lowercase()
                || byte.is_ascii_digit()
                || matches!(byte, b'_' | b'.' | b':' | b'-')
        })
}

/// The frozen v1 tool registry.
#[derive(Clone, Default)]
pub struct ToolRegistry {
    tools: BTreeMap<String, ToolSpec>,
}

impl ToolRegistry {
    /// An empty registry.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// The frozen v1 tool set (20 tools across the five capabilities).
    #[must_use]
    pub fn with_v1_tools() -> Self {
        let mut registry = Self::new();
        for spec in v1_tool_specs() {
            // The frozen set is constructed once from constants; a failure
            // here is a build-time contract bug, not a runtime condition.
            registry
                .register(spec)
                .expect("frozen v1 tool set must register");
        }
        registry
    }

    /// Registers a tool.  Deny-list hits, duplicates, invalid shapes and
    /// capacity overflow are stable rejections.
    pub fn register(&mut self, spec: ToolSpec) -> Result<(), RegistryError> {
        if !is_token(&spec.name, MAX_TOOL_NAME_LEN) {
            return Err(RegistryError::InvalidName);
        }
        if is_permanently_denied(&spec.name) {
            return Err(RegistryError::PermanentlyDenied);
        }
        if self.tools.contains_key(&spec.name) {
            return Err(RegistryError::DuplicateTool);
        }
        if self.tools.len() >= MAX_TOOLS {
            return Err(RegistryError::Capacity);
        }
        if spec.risk != spec.capability.risk_level() {
            return Err(RegistryError::RiskMismatch);
        }
        if spec.params.len() > MAX_PARAMS_PER_TOOL {
            return Err(RegistryError::TooManyParams);
        }
        for param in &spec.params {
            if !is_token(&param.key, MAX_PARAM_KEY_LEN) {
                return Err(RegistryError::InvalidParamKey);
            }
        }
        self.tools.insert(spec.name.clone(), spec);
        Ok(())
    }

    /// Looks up a tool by name.
    #[must_use]
    pub fn find(&self, name: &str) -> Option<&ToolSpec> {
        self.tools.get(name)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.tools.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.tools.is_empty()
    }

    /// Iterates tools in name order (deterministic snapshot order).
    pub fn iter(&self) -> impl Iterator<Item = &ToolSpec> {
        self.tools.values()
    }

    /// Deterministic snapshot lines, one per tool, in name order:
    /// `name|capability|risk|confirmation|availability|idempotent|streaming|params`.
    #[must_use]
    pub fn snapshot(&self) -> String {
        let mut out = String::new();
        for spec in self.tools.values() {
            let params: Vec<String> = spec
                .params
                .iter()
                .map(|param| format!("{}{}", param.key, if param.required { "!" } else { "?" }))
                .collect();
            out.push_str(&format!(
                "{}|{}|{}|{}|{}|{}|{}|{}\n",
                spec.name,
                spec.capability.wire_name(),
                spec.risk.wire_name(),
                spec.confirmation.wire_name(),
                spec.availability.wire_name(),
                spec.idempotent,
                spec.streaming,
                params.join(",")
            ));
        }
        out
    }
}

/// The frozen v1 tool declarations.
fn v1_tool_specs() -> Vec<ToolSpec> {
    vec![
        // R1 page reading.
        ToolSpec::build(
            "page.list_targets",
            AgentCapability::PageRead,
            true,
            false,
            &[],
        ),
        ToolSpec::build(
            "page.get_title",
            AgentCapability::PageRead,
            true,
            false,
            &[],
        ),
        ToolSpec::build(
            "page.get_selection",
            AgentCapability::PageRead,
            true,
            false,
            &[],
        ),
        ToolSpec::build(
            "page.snapshot",
            AgentCapability::PageRead,
            true,
            true,
            &[("format", false), ("max_bytes", false)],
        ),
        ToolSpec::build(
            "page.markdown",
            AgentCapability::PageRead,
            true,
            true,
            &[("max_bytes", false)],
        ),
        // R0/R1 cast reading.
        ToolSpec::build(
            "cast.list_receivers",
            AgentCapability::CastRead,
            true,
            false,
            &[],
        ),
        ToolSpec::build(
            "cast.get_state",
            AgentCapability::CastRead,
            true,
            false,
            &[],
        ),
        // R2 navigation.
        ToolSpec::build(
            "nav.open_tab",
            AgentCapability::Navigation,
            false,
            false,
            &[("url", true)],
        ),
        ToolSpec::build(
            "nav.switch_tab",
            AgentCapability::Navigation,
            true,
            false,
            &[],
        ),
        ToolSpec::build(
            "nav.close_tab",
            AgentCapability::Navigation,
            false,
            false,
            &[],
        ),
        ToolSpec::build(
            "nav.navigate",
            AgentCapability::Navigation,
            false,
            false,
            &[("url", true)],
        ),
        ToolSpec::build(
            "nav.go_back",
            AgentCapability::Navigation,
            false,
            false,
            &[],
        ),
        ToolSpec::build("nav.reload", AgentCapability::Navigation, false, false, &[]),
        ToolSpec::build(
            "nav.scroll",
            AgentCapability::Navigation,
            false,
            false,
            &[("delta_y", true)],
        ),
        // R3 cast control.
        ToolSpec::build(
            "cast.select_receiver",
            AgentCapability::CastControl,
            false,
            false,
            &[("receiver", true)],
        ),
        ToolSpec::build(
            "cast.start",
            AgentCapability::CastControl,
            false,
            false,
            &[],
        ),
        ToolSpec::build("cast.pause", AgentCapability::CastControl, true, false, &[]),
        ToolSpec::build(
            "cast.seek",
            AgentCapability::CastControl,
            false,
            false,
            &[("position_ms", true)],
        ),
        ToolSpec::build("cast.stop", AgentCapability::CastControl, true, false, &[]),
        // R4 semantic actions: preview-gated until the dedicated review.
        ToolSpec::build(
            "act.invoke",
            AgentCapability::SemanticAction,
            false,
            false,
            &[("action_id", true), ("args", false)],
        ),
    ]
}

#[cfg(test)]
#[path = "registry_tests.rs"]
mod tests;
