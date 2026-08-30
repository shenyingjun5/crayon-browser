//! R1 page content tools (AGT-07).
//!
//! The tool layer authorizes every read through the existing grant store,
//! then delegates to a Browser-owned app-runtime port. Page content never
//! participates in authorization or target selection. The port must fence
//! profile, foreground state and navigation generation before returning any
//! content.

use crate::grant::{GrantError, GrantManager, ProfileScope};
use crayon_content_markdown::{render_snapshot, MarkdownDocument, MarkdownError};
use crayon_domain::{AgentCapability, AgentTarget, CaapError, SessionGeneration, TabId};
use crayon_page_data::{limits, PageSnapshot};
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum targets returned by one profile-scoped listing.
pub const MAX_CONTENT_TARGETS: usize = 64;
/// Maximum Browser-verified selection exposed to an R1 caller.
pub const MAX_SELECTION_BYTES: usize = 16 * 1024;
/// Maximum serialized structured snapshot accepted by one logical read.
pub const MAX_SNAPSHOT_OUTPUT_BYTES: usize = 2 * 1024 * 1024;

/// Sanitized target metadata. No URL, profile id or page body is exposed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ContentTarget {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub title: String,
    pub active: bool,
}

/// A title result retains its generation fence.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageTitle {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub title: String,
}

/// A selection result retains its generation fence.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PageSelection {
    pub tab_id: TabId,
    pub generation: SessionGeneration,
    pub text: String,
}

/// Stable Browser-owned read rejection. Variants carry no page data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContentReadRejection {
    TargetInvalid,
    BackgroundTarget,
    StaleGeneration,
    SourceUnavailable,
    SelectionTooLarge,
    OutputTooLarge,
    CapacityExceeded,
    Cancelled,
}

impl Display for ContentReadRejection {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::TargetInvalid => "content target is not available in this profile",
            Self::BackgroundTarget => "background target cannot expose page content",
            Self::StaleGeneration => "content target generation is stale",
            Self::SourceUnavailable => "content source is unavailable",
            Self::SelectionTooLarge => "selection exceeds the R1 bound",
            Self::OutputTooLarge => "content output exceeds the requested bound",
            Self::CapacityExceeded => "content target capacity is exceeded",
            Self::Cancelled => "content read was cancelled",
        })
    }
}

impl Error for ContentReadRejection {}

/// Combined authorization/source failure for an R1 tool call.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ContentReadError {
    Grant(GrantError),
    Rejected(ContentReadRejection),
    InvalidLimit,
}

impl ContentReadError {
    #[must_use]
    pub const fn to_caap_error(self) -> CaapError {
        match self {
            Self::Grant(error) => error.to_caap_error(),
            Self::InvalidLimit => CaapError::InvalidMessage,
            Self::Rejected(ContentReadRejection::TargetInvalid) => CaapError::TargetInvalid,
            Self::Rejected(ContentReadRejection::BackgroundTarget)
            | Self::Rejected(ContentReadRejection::SourceUnavailable) => {
                CaapError::CapabilityDenied
            }
            Self::Rejected(ContentReadRejection::StaleGeneration) => CaapError::TargetStale,
            Self::Rejected(ContentReadRejection::SelectionTooLarge)
            | Self::Rejected(ContentReadRejection::OutputTooLarge)
            | Self::Rejected(ContentReadRejection::CapacityExceeded) => CaapError::QueueFull,
            Self::Rejected(ContentReadRejection::Cancelled) => CaapError::Cancelled,
        }
    }
}

impl Display for ContentReadError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Grant(error) => write!(formatter, "{error}"),
            Self::Rejected(error) => write!(formatter, "{error}"),
            Self::InvalidLimit => formatter.write_str("content output limit is invalid"),
        }
    }
}

impl Error for ContentReadError {}

impl From<GrantError> for ContentReadError {
    fn from(value: GrantError) -> Self {
        Self::Grant(value)
    }
}

impl From<ContentReadRejection> for ContentReadError {
    fn from(value: ContentReadRejection) -> Self {
        Self::Rejected(value)
    }
}

/// Browser-owned source port. Implementations return content only after
/// profile, active-tab and generation checks; there is no raw DOM/CEF API.
pub trait ContentReadPort {
    fn list_targets(
        &self,
        profile: &ProfileScope,
    ) -> Result<Vec<ContentTarget>, ContentReadRejection>;
    fn get_title(
        &self,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
    ) -> Result<PageTitle, ContentReadRejection>;
    fn get_selection(
        &self,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
    ) -> Result<PageSelection, ContentReadRejection>;
    fn get_snapshot(
        &self,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
    ) -> Result<PageSnapshot, ContentReadRejection>;
}

/// Validates a Browser-provided selection before it enters runtime state.
pub fn validate_selection(value: &str) -> Result<(), ContentReadRejection> {
    if value.len() > MAX_SELECTION_BYTES
        || value
            .chars()
            .any(|ch| ch == '\0' || (ch.is_control() && !matches!(ch, '\n' | '\r' | '\t')))
    {
        return Err(ContentReadRejection::SelectionTooLarge);
    }
    Ok(())
}

fn validate_limit(max_bytes: usize, hard_max: usize) -> Result<(), ContentReadError> {
    if max_bytes == 0 || max_bytes > hard_max {
        Err(ContentReadError::InvalidLimit)
    } else {
        Ok(())
    }
}

fn validate_binding(
    requested: &AgentTarget,
    generation: SessionGeneration,
    actual_tab: &TabId,
    actual_generation: SessionGeneration,
) -> Result<(), ContentReadError> {
    if actual_generation != generation
        || matches!(requested, AgentTarget::Tab { tab } if tab != actual_tab)
    {
        return Err(ContentReadRejection::StaleGeneration.into());
    }
    Ok(())
}

/// One request-scoped R1 facade. The mutable grant reference ensures a
/// successful authorization is accounted exactly once per tool call.
pub struct ContentReader<'a> {
    grants: &'a mut GrantManager,
    port: &'a dyn ContentReadPort,
}

impl<'a> ContentReader<'a> {
    pub fn new(grants: &'a mut GrantManager, port: &'a dyn ContentReadPort) -> Self {
        Self { grants, port }
    }

    pub fn list_targets(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        now_ms: u64,
    ) -> Result<Vec<ContentTarget>, ContentReadError> {
        self.authorize(session, profile, None, now_ms)?;
        let targets = self.port.list_targets(profile)?;
        let active_count = targets.iter().filter(|target| target.active).count();
        let has_duplicate = targets.iter().enumerate().any(|(index, target)| {
            targets[index + 1..]
                .iter()
                .any(|other| other.tab_id == target.tab_id)
        });
        if targets.len() > MAX_CONTENT_TARGETS
            || active_count > 1
            || has_duplicate
            || targets.iter().any(|target| {
                target.title.is_empty()
                    || target.title.len() > limits::MAX_TITLE_BYTES
                    || target.title.contains(['\n', '\r', '\t'])
            })
        {
            return Err(ContentReadRejection::OutputTooLarge.into());
        }
        Ok(targets)
    }

    pub fn get_title(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
        now_ms: u64,
    ) -> Result<PageTitle, ContentReadError> {
        self.authorize(session, profile, Some(target), now_ms)?;
        let title = self.port.get_title(profile, target, generation)?;
        validate_binding(target, generation, &title.tab_id, title.generation)?;
        if title.title.is_empty()
            || title.title.len() > limits::MAX_TITLE_BYTES
            || title.title.contains(['\n', '\r', '\t'])
        {
            return Err(ContentReadRejection::OutputTooLarge.into());
        }
        Ok(title)
    }

    pub fn get_selection(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
        now_ms: u64,
    ) -> Result<PageSelection, ContentReadError> {
        self.authorize(session, profile, Some(target), now_ms)?;
        let selection = self.port.get_selection(profile, target, generation)?;
        validate_binding(target, generation, &selection.tab_id, selection.generation)?;
        validate_selection(&selection.text)?;
        Ok(selection)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn get_snapshot(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
        max_bytes: usize,
        now_ms: u64,
    ) -> Result<PageSnapshot, ContentReadError> {
        validate_limit(max_bytes, MAX_SNAPSHOT_OUTPUT_BYTES)?;
        self.authorize(session, profile, Some(target), now_ms)?;
        let snapshot = self.port.get_snapshot(profile, target, generation)?;
        validate_binding(
            target,
            generation,
            &snapshot.navigation().tab_id,
            snapshot.navigation().generation,
        )?;
        let encoded = serde_json::to_vec(&snapshot)
            .map_err(|_| ContentReadError::Rejected(ContentReadRejection::SourceUnavailable))?;
        if encoded.len() > max_bytes {
            return Err(ContentReadRejection::OutputTooLarge.into());
        }
        Ok(snapshot)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn get_markdown(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        target: &AgentTarget,
        generation: SessionGeneration,
        max_bytes: usize,
        now_ms: u64,
    ) -> Result<MarkdownDocument, ContentReadError> {
        validate_limit(
            max_bytes,
            crayon_content_markdown::STANDARD_MAX_MARKDOWN_BYTES,
        )?;
        self.authorize(session, profile, Some(target), now_ms)?;
        let snapshot = self.port.get_snapshot(profile, target, generation)?;
        validate_binding(
            target,
            generation,
            &snapshot.navigation().tab_id,
            snapshot.navigation().generation,
        )?;
        let document = render_snapshot(&snapshot).map_err(|error| match error {
            MarkdownError::OutputTooLarge => ContentReadRejection::OutputTooLarge,
            MarkdownError::FormattingFailed => ContentReadRejection::SourceUnavailable,
        })?;
        if document.markdown().len() > max_bytes {
            return Err(ContentReadRejection::OutputTooLarge.into());
        }
        Ok(document)
    }

    fn authorize(
        &mut self,
        session: &str,
        profile: &ProfileScope,
        target: Option<&AgentTarget>,
        now_ms: u64,
    ) -> Result<(), ContentReadError> {
        self.grants
            .authorize(session, profile, AgentCapability::PageRead, target, now_ms)
            .map(|_| ())
            .map_err(ContentReadError::Grant)
    }
}
