//! `CandidateStore`: normalizes and merges observations into candidates
//! (MED-02).
//!
//! Privacy contract (PL-002, relay/mirror invariants):
//! - the full URL (including short-lived signature query) lives only in this
//!   trusted in-memory store — merging never strips the query;
//! - logs/DTOs/diagnostics see only `RedactedCandidate` (opaque id + origin);
//! - `CandidateEntry` has no `Serialize` and a redacting `Debug`.

use crate::observation::{FrameContext, NavigationId, ObservationSource, SourceObservation};
use crayon_domain::TabId;
use std::fmt::{Debug, Formatter};

/// Maximum evidence entries kept per candidate (bounded collection rule).
pub const MAX_EVIDENCE: usize = 8;
/// Maximum candidates per store (bounded collection rule; lifecycle
/// eviction arrives with MED-04).
pub const MAX_CANDIDATES: usize = 256;

/// Opaque candidate identifier assigned by the store.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct CandidateId(u64);

impl CandidateId {
    #[must_use]
    pub const fn get(self) -> u64 {
        self.0
    }
}

/// One piece of evidence supporting a candidate (PL-001).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Evidence {
    pub source: ObservationSource,
    pub frame: FrameContext,
    pub observed_at_ms: u64,
}

/// A merged media candidate. The full URL stays in trusted memory.
pub struct CandidateEntry {
    id: CandidateId,
    /// Full original URL (query preserved byte-exact; PL-002).
    url: String,
    /// `scheme://host[:port]` only — safe for logs.
    redacted_origin: String,
    tab_id: TabId,
    navigation: NavigationId,
    evidence: Vec<Evidence>,
}

impl CandidateEntry {
    #[must_use]
    pub const fn id(&self) -> CandidateId {
        self.id
    }

    /// Full URL including any signature query. Trusted callers only — never
    /// pass to logs, DTOs, receivers, or the cloud.
    #[must_use]
    pub fn url(&self) -> &str {
        &self.url
    }

    /// `scheme://host[:port]` redacted origin, safe for logs/diagnostics.
    #[must_use]
    pub fn redacted_origin(&self) -> &str {
        &self.redacted_origin
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
    pub fn evidence(&self) -> &[Evidence] {
        &self.evidence
    }
}

impl Debug for CandidateEntry {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("CandidateEntry")
            .field("id", &self.id)
            .field("origin", &self.redacted_origin)
            .field("tab", &self.tab_id)
            .field("navigation", &self.navigation)
            .field("evidence", &self.evidence.len())
            .finish_non_exhaustive()
    }
}

/// Log/DTO-safe view of a candidate: opaque id + redacted origin only.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct RedactedCandidate {
    pub id: CandidateId,
    pub origin: String,
}

/// In-memory candidate store with URL-normalized merging.
#[derive(Default)]
pub struct CandidateStore {
    entries: Vec<CandidateEntry>,
    next_id: u64,
}

/// URL normalization for merging: scheme/host case and default ports are
/// canonicalized by the URL parser; path and query stay byte-exact.
/// Returns `None` for non-absolute/non-http(s) input (should not happen —
/// observations are validated — but never merge on a guess).
fn merge_key(url: &str) -> Option<String> {
    let parsed = url::Url::parse(url).ok()?;
    match parsed.scheme() {
        "http" | "https" => Some(parsed.to_string()),
        _ => None,
    }
}

fn redacted_origin(url: &str) -> String {
    match url::Url::parse(url) {
        Ok(parsed) => {
            let mut origin = format!("{}://{}", parsed.scheme(), parsed.host_str().unwrap_or(""));
            if let Some(port) = parsed.port() {
                origin.push_str(&format!(":{port}"));
            }
            origin
        }
        Err(_) => String::new(),
    }
}

impl CandidateStore {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Ingests one observation. Same normalized URL within the same
    /// tab+navigation merges into the existing candidate and records the
    /// evidence (PL-001); otherwise a new candidate is created.
    ///
    /// Returns `None` when the store is full (bounded capacity rule) or the
    /// URL cannot form a merge key.
    pub fn ingest(&mut self, observation: &SourceObservation) -> Option<CandidateId> {
        let key = merge_key(observation.url())?;
        if let Some(entry) = self.entries.iter_mut().find(|e| {
            e.tab_id == *observation.tab_id()
                && e.navigation == observation.navigation()
                && merge_key(&e.url).as_deref() == Some(key.as_str())
        }) {
            let evidence = Evidence {
                source: observation.source(),
                frame: observation.frame(),
                observed_at_ms: observation.observed_at_ms(),
            };
            if entry.evidence.len() < MAX_EVIDENCE
                && !entry
                    .evidence
                    .iter()
                    .any(|e| e.source == evidence.source && e.frame == evidence.frame)
            {
                entry.evidence.push(evidence);
            }
            return Some(entry.id);
        }
        if self.entries.len() >= MAX_CANDIDATES {
            return None;
        }
        let id = CandidateId(self.next_id);
        self.next_id += 1;
        self.entries.push(CandidateEntry {
            id,
            url: observation.url().to_string(),
            redacted_origin: redacted_origin(observation.url()),
            tab_id: observation.tab_id().clone(),
            navigation: observation.navigation(),
            evidence: vec![Evidence {
                source: observation.source(),
                frame: observation.frame(),
                observed_at_ms: observation.observed_at_ms(),
            }],
        });
        Some(id)
    }

    #[must_use]
    pub fn get(&self, id: CandidateId) -> Option<&CandidateEntry> {
        self.entries.iter().find(|e| e.id == id)
    }

    /// Log/DTO-safe view (opaque id + redacted origin).
    #[must_use]
    pub fn redacted(&self, id: CandidateId) -> Option<RedactedCandidate> {
        self.get(id).map(|e| RedactedCandidate {
            id: e.id,
            origin: e.redacted_origin.clone(),
        })
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}
