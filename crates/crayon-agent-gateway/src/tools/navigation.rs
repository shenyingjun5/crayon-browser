//! R2 navigation tools (AGT-09): `nav.navigate`, `nav.open_tab`,
//! `nav.close_tab`, `nav.switch_tab`, `nav.back`, `nav.reload` and
//! `nav.scroll`.
//!
//! The tool layer validates bounds and the user-confirmation binding,
//! rejects dangerous navigation targets before anything reaches the
//! engine, and renders deterministic summaries for receipts. Execution
//! enters through the [`NavigationPort`] implemented by app-runtime on
//! top of normal browser use cases; this layer never touches CEF or the
//! engine directly.

use crayon_domain::CaapError;
use crayon_page_data::is_safe_url;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum scroll distance in pixels for one bounded step.
pub const MAX_SCROLL_PIXELS: u32 = 10_000;

/// Maximum length of a confirmation reference.
pub const MAX_CONFIRMATION_BYTES: usize = 128;

/// Closed R2 navigation verbs.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NavigationTool {
    Navigate,
    OpenTab,
    CloseTab,
    SwitchTab,
    GoBack,
    Reload,
    Scroll,
}

impl NavigationTool {
    /// Stable wire name used by receipts.
    #[must_use]
    pub const fn wire_name(self) -> &'static str {
        match self {
            Self::Navigate => "nav.navigate",
            Self::OpenTab => "nav.open_tab",
            Self::CloseTab => "nav.close_tab",
            Self::SwitchTab => "nav.switch_tab",
            Self::GoBack => "nav.back",
            Self::Reload => "nav.reload",
            Self::Scroll => "nav.scroll",
        }
    }
}

/// Closed scroll directions.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScrollDirection {
    Up,
    Down,
    Top,
    Bottom,
}

/// One validated navigation request. Construction is the validation
/// gate: hostile targets and out-of-bounds values are not expressible.
#[derive(Clone, Debug, PartialEq)]
pub struct NavigationRequest {
    pub tool: NavigationTool,
    /// The page generation the request was built at; stale requests are
    /// fenced by the execution layer.
    pub generation: u64,
    /// HTTP(S) target for `Navigate`/`OpenTab`; rejected otherwise.
    pub url: Option<String>,
    /// Target tab for tab-scoped verbs.
    pub tab_id: Option<String>,
    /// Scroll direction and bounded pixel step for `Scroll`.
    pub scroll: Option<(ScrollDirection, u32)>,
    /// The user confirmation bound to this request (R2 gate).
    pub confirmation: String,
}

/// Request validation failure. Stable variants carry no content.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NavigationInputError {
    DangerousScheme,
    InvalidUrl,
    MissingTab,
    MissingConfirmation,
    InvalidConfirmation,
    MissingScroll,
    ScrollOutOfBounds,
    UnexpectedUrl,
    UnexpectedTab,
    UnexpectedScroll,
}

impl Display for NavigationInputError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::DangerousScheme => "navigation target scheme is not permitted",
            Self::InvalidUrl => "navigation target is not a valid http(s) URL",
            Self::MissingTab => "request is missing a target tab",
            Self::MissingConfirmation => "request is missing the user confirmation",
            Self::InvalidConfirmation => "confirmation reference is malformed",
            Self::MissingScroll => "request is missing scroll parameters",
            Self::ScrollOutOfBounds => "scroll distance exceeds the bounded step",
            Self::UnexpectedUrl => "this tool does not take a URL",
            Self::UnexpectedTab => "this tool does not take a target tab",
            Self::UnexpectedScroll => "this tool does not take scroll parameters",
        })
    }
}

impl Error for NavigationInputError {}

fn validate_confirmation(confirmation: &str) -> Result<(), NavigationInputError> {
    if confirmation.is_empty() {
        return Err(NavigationInputError::MissingConfirmation);
    }
    if confirmation.len() > MAX_CONFIRMATION_BYTES
        || !confirmation
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
    {
        return Err(NavigationInputError::InvalidConfirmation);
    }
    Ok(())
}

impl NavigationRequest {
    /// Validates and wraps one request; every verb has a closed field set
    /// and the URL gate rejects every non-HTTP(S) target.
    pub fn new(
        tool: NavigationTool,
        generation: u64,
        url: Option<String>,
        tab_id: Option<String>,
        scroll: Option<(ScrollDirection, u32)>,
        confirmation: &str,
    ) -> Result<Self, NavigationInputError> {
        validate_confirmation(confirmation)?;
        let needs_url = matches!(tool, NavigationTool::Navigate | NavigationTool::OpenTab);
        let needs_tab = !matches!(tool, NavigationTool::OpenTab);
        let needs_scroll = tool == NavigationTool::Scroll;
        if needs_url {
            let Some(url) = &url else {
                return Err(NavigationInputError::InvalidUrl);
            };
            // The safe-URL gate rejects non-http(s) schemes, userinfo,
            // control characters, over-long and malformed authorities —
            // `javascript:`, `file:`, `data:` and friends never pass.
            if !is_safe_url(url) {
                return Err(NavigationInputError::DangerousScheme);
            }
        } else if url.is_some() {
            return Err(NavigationInputError::UnexpectedUrl);
        }
        if needs_tab && tab_id.is_none() {
            return Err(NavigationInputError::MissingTab);
        }
        if !needs_tab && tab_id.is_some() {
            return Err(NavigationInputError::UnexpectedTab);
        }
        if needs_scroll {
            let Some((_, pixels)) = scroll else {
                return Err(NavigationInputError::MissingScroll);
            };
            if pixels == 0 || pixels > MAX_SCROLL_PIXELS {
                return Err(NavigationInputError::ScrollOutOfBounds);
            }
        } else if scroll.is_some() {
            return Err(NavigationInputError::UnexpectedScroll);
        }
        Ok(Self {
            tool,
            generation,
            url,
            tab_id,
            scroll,
            confirmation: confirmation.to_owned(),
        })
    }

    /// Deterministic receipt summary: tool, target shape and bounds only —
    /// never the full URL query, page content or confirmation value.
    #[must_use]
    pub fn receipt_summary(&self) -> String {
        let target = match (&self.url, &self.scroll, &self.tab_id) {
            (Some(url), _, _) => {
                let authority = url.split("://").nth(1).unwrap_or_default();
                let authority = authority.split(['/', '?', '#']).next().unwrap_or_default();
                format!("url:{authority}")
            }
            (None, Some((direction, pixels)), _) => {
                format!("scroll:{direction:?}:{pixels}")
            }
            (None, None, Some(tab)) => format!("tab:{tab}"),
            _ => "none".to_owned(),
        };
        format!("{} {target} confirmed", self.tool.wire_name())
    }
}

/// Port rejection from the execution layer. Stable variants.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NavigationRejection {
    /// The engine adapter is not available.
    PortUnavailable,
    /// The target tab is unknown to the runtime.
    UnknownTab,
    /// The tab is already at the open-tab capacity.
    TabCapacityExceeded,
    /// The page generation advanced after the request was built.
    StaleGeneration,
    /// The engine rejected the navigation (dangerous redirect, download,
    /// blocked target).
    EngineRejected,
}

/// Execution result of one dispatched request.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NavigationOutcome {
    Accepted,
    Rejected(NavigationRejection),
}

/// Port to the normal browser navigation use cases (implemented by
/// app-runtime). Cancellation and deadline semantics live behind the
/// engine adapter; the port reports only terminal outcomes.
pub trait NavigationPort {
    fn execute(&mut self, request: &NavigationRequest) -> NavigationOutcome;
}

/// Tool-layer failure mapping into CAAP error codes.
#[must_use]
pub const fn to_caap_error(error: NavigationInputError) -> CaapError {
    match error {
        NavigationInputError::DangerousScheme
        | NavigationInputError::InvalidUrl
        | NavigationInputError::InvalidConfirmation
        | NavigationInputError::ScrollOutOfBounds
        | NavigationInputError::UnexpectedUrl
        | NavigationInputError::UnexpectedTab
        | NavigationInputError::UnexpectedScroll
        | NavigationInputError::MissingTab
        | NavigationInputError::MissingScroll => CaapError::InvalidMessage,
        NavigationInputError::MissingConfirmation => CaapError::CapabilityDenied,
    }
}

#[must_use]
pub const fn rejection_to_caap_error(rejection: NavigationRejection) -> CaapError {
    match rejection {
        NavigationRejection::PortUnavailable => CaapError::CapabilityDenied,
        NavigationRejection::UnknownTab => CaapError::InvalidMessage,
        NavigationRejection::TabCapacityExceeded => CaapError::QueueFull,
        NavigationRejection::StaleGeneration => CaapError::TargetStale,
        NavigationRejection::EngineRejected => CaapError::CapabilityDenied,
    }
}
