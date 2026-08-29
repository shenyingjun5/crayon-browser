//! Thread-safe app-runtime ownership seam for verified current-page snapshots.

use crayon_domain::{SessionGeneration, TabId};
use crayon_page_data::{
    OwnerError, PageSnapshot, PublishResult, SnapshotOwner, SnapshotOwnerStats, SnapshotPage,
    SnapshotReadId,
};
use std::sync::{Mutex, MutexGuard};

/// The sole app-runtime writer for page snapshot state. The mutex protects
/// only in-memory state; methods perform no callbacks, network or file IO.
#[derive(Default)]
pub struct PageSnapshotRuntime {
    owner: Mutex<SnapshotOwner>,
}

impl PageSnapshotRuntime {
    pub fn publish(&self, snapshot: PageSnapshot) -> Result<PublishResult, OwnerError> {
        self.lock().publish(snapshot)
    }

    pub fn advance_navigation(
        &self,
        tab_id: TabId,
        generation: SessionGeneration,
    ) -> Result<bool, OwnerError> {
        self.lock().advance_navigation(tab_id, generation)
    }

    pub fn begin_read(
        &self,
        tab_id: &TabId,
        generation: SessionGeneration,
        page_size: usize,
    ) -> Result<SnapshotReadId, OwnerError> {
        self.lock().begin_read(tab_id, generation, page_size)
    }

    pub fn next_page(&self, read_id: SnapshotReadId) -> Result<SnapshotPage, OwnerError> {
        self.lock().next_page(read_id)
    }

    pub fn cancel(&self, read_id: SnapshotReadId) -> Result<bool, OwnerError> {
        self.lock().cancel(read_id)
    }

    pub fn close_tab(&self, tab_id: &TabId) -> Result<bool, OwnerError> {
        self.lock().close_tab(tab_id)
    }

    pub fn shut_down(&self) {
        self.lock().shut_down();
    }

    #[must_use]
    pub fn stats(&self) -> SnapshotOwnerStats {
        self.lock().stats()
    }

    fn lock(&self) -> MutexGuard<'_, SnapshotOwner> {
        self.owner
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }
}

#[cfg(test)]
#[path = "page_snapshot_runtime_tests.rs"]
mod tests;
