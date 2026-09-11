//! Skill version history with rollback (WFL-13).
//!
//! Every upgrade pushes the previous recipe onto a bounded per-skill
//! history. Rollback restores the most recent previous content while the
//! stored revision keeps advancing monotonically (via the WFL-10 store's
//! `upgrade`), so version numbers never move backwards.

use std::collections::BTreeMap;

use crayon_domain::Recipe;

/// Maximum history entries per skill; the oldest is evicted.
pub const MAX_HISTORY_PER_SKILL: usize = 8;

/// A prior recipe captured at upgrade time.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PriorVersion {
    pub recipe: Recipe,
    /// The revision this content had when it was current.
    pub revision: u64,
}

/// Failure of history operations; content-free.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HistoryError {
    /// No prior version exists to roll back to.
    NoPriorVersion,
    /// The history is full and eviction is disabled for this call.
    Full,
}

/// Bounded per-skill version history.
#[derive(Default)]
pub struct VersionHistory {
    entries: BTreeMap<String, Vec<PriorVersion>>,
}

impl VersionHistory {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Captures the current recipe before an upgrade. Oldest entries are
    /// evicted once the per-skill bound is reached.
    pub fn capture(&mut self, name: &str, recipe: Recipe, revision: u64) {
        let history = self.entries.entry(name.to_owned()).or_default();
        history.push(PriorVersion { recipe, revision });
        if history.len() > MAX_HISTORY_PER_SKILL {
            history.remove(0);
        }
    }

    /// The recipe to roll back to (the most recent prior version), or
    /// `None` when the skill has no history.
    #[must_use]
    pub fn previous(&self, name: &str) -> Option<&PriorVersion> {
        self.entries.get(name).and_then(|h| h.last())
    }

    /// Consumes the most recent prior version for a rollback. The caller
    /// re-stores it via the WFL-10 store's `upgrade` (revision advances).
    pub fn pop_previous(&mut self, name: &str) -> Option<PriorVersion> {
        let mut history = self.entries.remove(name)?;
        let prior = history.pop()?;
        if !history.is_empty() {
            self.entries.insert(name.to_owned(), history);
        }
        Some(prior)
    }

    #[must_use]
    pub fn history_len(&self, name: &str) -> usize {
        self.entries.get(name).map(Vec::len).unwrap_or(0)
    }
}

#[cfg(test)]
mod version_tests;
