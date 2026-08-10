//! Candidate lifecycle (MED-04): navigation invalidation, TTL expiry,
//! tab-close tombstones and bounded capacity with eviction.
//!
//! Time is always caller-supplied logical milliseconds — shared code never
//! reads the wall clock (tests drive time explicitly).

use super::store::CandidateStore;
use crate::observation::NavigationId;
use crayon_domain::TabId;

/// Lifecycle policy knobs.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LifecyclePolicy {
    ttl_ms: u64,
}

impl LifecyclePolicy {
    /// Default candidate TTL: 10 minutes.
    pub const DEFAULT: Self = Self {
        ttl_ms: 10 * 60 * 1000,
    };

    #[must_use]
    pub const fn new(ttl_ms: u64) -> Self {
        Self { ttl_ms }
    }

    #[must_use]
    pub const fn ttl_ms(self) -> u64 {
        self.ttl_ms
    }
}

impl Default for LifecyclePolicy {
    fn default() -> Self {
        Self::DEFAULT
    }
}

impl CandidateStore {
    /// Top-level navigation: candidates from older navigations of this tab
    /// are dropped and the tab's current navigation advances (BR-007).
    /// Returns the number of dropped candidates. Idempotent.
    pub fn on_navigation(&mut self, tab_id: &TabId, navigation: NavigationId) -> usize {
        if let Some(tab) = self.tabs.iter_mut().find(|t| t.tab_id == *tab_id) {
            if navigation > tab.current_navigation {
                tab.current_navigation = navigation;
            }
            tab.closed_at_navigation = None;
        } else if self.tabs.len() < super::store::MAX_TABS {
            self.tabs.push(super::store::TabState {
                tab_id: tab_id.clone(),
                current_navigation: navigation,
                closed_at_navigation: None,
            });
        }
        let before = self.entries.len();
        self.entries
            .retain(|e| !(e.tab_id == *tab_id && e.navigation < navigation));
        before - self.entries.len()
    }

    /// Tab/window closed (BR-013): all its candidates are dropped and a
    /// tombstone blocks late events from re-creating candidates. A later
    /// navigation re-opens the tab. Idempotent; returns dropped count.
    pub fn on_tab_close(&mut self, tab_id: &TabId) -> usize {
        let current = self
            .tabs
            .iter()
            .find(|t| t.tab_id == *tab_id)
            .map(|t| t.current_navigation);
        if let Some(current) = current {
            if let Some(tab) = self.tabs.iter_mut().find(|t| t.tab_id == *tab_id) {
                tab.closed_at_navigation = Some(current);
            }
        }
        let before = self.entries.len();
        self.entries.retain(|e| e.tab_id != *tab_id);
        before - self.entries.len()
    }

    /// TTL expiry (PL-012): candidates whose latest evidence is older than
    /// the policy TTL relative to `now_ms` are dropped; planners must
    /// re-plan instead of reusing them. Returns the expired count.
    pub fn expire_stale(&mut self, now_ms: u64, policy: LifecyclePolicy) -> usize {
        let ttl = policy.ttl_ms();
        let before = self.entries.len();
        self.entries
            .retain(|e| now_ms <= e.last_observed_ms.saturating_add(ttl));
        before - self.entries.len()
    }
}
