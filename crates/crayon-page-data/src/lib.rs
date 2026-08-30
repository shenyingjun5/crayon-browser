//! Verified, bounded current-page data contracts.

mod delta;
mod index;
mod owner;
mod snapshot;

pub use delta::{
    DeltaChunk, DeltaError, DeltaKind, DeltaMetadata, DeltaStream, SnapshotDelta, MAX_DELTA_BLOCKS,
    MAX_DELTA_CHUNK_BLOCKS, MAX_UNACKED_DELTA_CHUNKS,
};
pub use index::{BlockKind, SnapshotIndex};
pub use owner::{
    OwnerError, PublishResult, SnapshotOwner, SnapshotOwnerStats, SnapshotPage, SnapshotReadId,
    MAX_ACTIVE_READS, MAX_CACHED_TABS, MAX_PAGE_BLOCKS, MAX_RETIRED_READS,
};

pub use snapshot::{
    is_safe_url, limits, ContentBlock, NavigationBinding, OutputLevel, PageSnapshot, Provenance,
    SnapshotError, TableRow, TruncationInfo, TruncationReason, VERIFIED_BY_BROWSER_PROCESS,
};
