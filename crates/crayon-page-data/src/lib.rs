//! Verified, bounded current-page data contracts.

mod owner;
mod snapshot;

pub use owner::{
    OwnerError, PublishResult, SnapshotOwner, SnapshotOwnerStats, SnapshotPage, SnapshotReadId,
    MAX_ACTIVE_READS, MAX_CACHED_TABS, MAX_PAGE_BLOCKS, MAX_RETIRED_READS,
};

pub use snapshot::{
    is_safe_url, limits, ContentBlock, NavigationBinding, OutputLevel, PageSnapshot, Provenance,
    SnapshotError, TableRow, TruncationInfo, TruncationReason, VERIFIED_BY_BROWSER_PROCESS,
};
