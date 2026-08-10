//! Validated source observations (MED-01).
//!
//! A `SourceObservation` is a fact reported by the browser layer about one
//! media candidate: which tab/navigation/frame it belongs to, which layer saw
//! it, and the URLs involved. Validation happens once at construction:
//! non-http(s), empty, or over-long URLs never enter the system. Observations
//! carry no page body, form data, or cookie content by construction (BR-008).
//!
//! Stale-event rule (BR-007): every observation is bound to a
//! `NavigationId`; after navigation the browser layer bumps the id and any
//! late event from an old frame/worker fails `is_current` and must be dropped.

use crayon_domain::TabId;
use std::error::Error;
use std::fmt::{Display, Formatter};

/// Maximum accepted URL length (bounded input rule).
const MAX_URL_LEN: usize = 2048;

/// Per-tab navigation counter assigned by the browser process; incremented on
/// every top-level navigation.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct NavigationId(u64);

impl NavigationId {
    /// First navigation of a tab.
    pub const INITIAL: Self = Self(0);

    #[must_use]
    pub const fn new(value: u64) -> Self {
        Self(value)
    }

    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }
}

/// Frame an observation belongs to.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum FrameContext {
    /// Top-level frame of the tab.
    Main,
    /// A subframe (iframe). The browser layer owns the concrete frame id;
    /// the observation only records that it was not the main frame.
    Subframe,
}

/// Which layer observed the media candidate (BR-008).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ObservationSource {
    /// `video`/`audio` element or `source` tag in the DOM.
    DomMediaElement,
    /// The element's resolved `currentSrc` while playing.
    CurrentSrc,
    /// A network request seen by the browser's network observer.
    NetworkRequest,
    /// An MSE `SourceBuffer` append.
    MseSourceBuffer,
    /// A fetch/XHR inside a dedicated Worker.
    WorkerFetch,
    /// A PerformanceObserver resource entry.
    PerformanceEntry,
}

/// Observation validation failure (stable variants).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ObservationError {
    EmptyUrl,
    UrlTooLong,
    /// Only `http://` and `https://` URLs may enter the media pipeline.
    UnsupportedScheme,
}

impl Display for ObservationError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::EmptyUrl => "observation url must not be empty",
            Self::UrlTooLong => "observation url exceeds the maximum length",
            Self::UnsupportedScheme => "observation url must use http or https",
        };
        f.write_str(message)
    }
}

impl Error for ObservationError {}

/// A validated media-source observation fact.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SourceObservation {
    tab_id: TabId,
    navigation: NavigationId,
    frame: FrameContext,
    source: ObservationSource,
    /// Observed media candidate URL (validated http/https).
    url: String,
    /// Top-level page URL at observation time (validated http/https).
    page_url: String,
    /// Logical observation timestamp in milliseconds, supplied by the
    /// browser layer (shared code never reads the wall clock).
    observed_at_ms: u64,
}

impl SourceObservation {
    /// Creates a validated observation; both URLs are checked.
    pub fn new(
        tab_id: TabId,
        navigation: NavigationId,
        frame: FrameContext,
        source: ObservationSource,
        url: String,
        page_url: String,
        observed_at_ms: u64,
    ) -> Result<Self, ObservationError> {
        validate_url(&url)?;
        validate_url(&page_url)?;
        Ok(Self {
            tab_id,
            navigation,
            frame,
            source,
            url,
            page_url,
            observed_at_ms,
        })
    }

    #[must_use]
    pub const fn tab_id(&self) -> &TabId {
        &self.tab_id
    }

    #[must_use]
    pub const fn navigation(&self) -> NavigationId {
        self.navigation
    }

    #[must_use]
    pub const fn frame(&self) -> FrameContext {
        self.frame
    }

    #[must_use]
    pub const fn source(&self) -> ObservationSource {
        self.source
    }

    #[must_use]
    pub fn url(&self) -> &str {
        &self.url
    }

    #[must_use]
    pub fn page_url(&self) -> &str {
        &self.page_url
    }

    #[must_use]
    pub const fn observed_at_ms(&self) -> u64 {
        self.observed_at_ms
    }

    /// True while the observation belongs to the tab's current navigation.
    /// Late events from a previous navigation (old frame/worker reports)
    /// return false and must be discarded (BR-007).
    #[must_use]
    pub fn is_current(&self, current_navigation: NavigationId) -> bool {
        self.navigation == current_navigation
    }
}

fn validate_url(url: &str) -> Result<(), ObservationError> {
    if url.is_empty() {
        return Err(ObservationError::EmptyUrl);
    }
    if url.len() > MAX_URL_LEN {
        return Err(ObservationError::UrlTooLong);
    }
    if !(url.starts_with("http://") || url.starts_with("https://")) {
        return Err(ObservationError::UnsupportedScheme);
    }
    Ok(())
}
