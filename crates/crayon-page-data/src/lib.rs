//! Verified, bounded current-page data contracts.

mod snapshot;

pub use snapshot::{
    is_safe_url, limits, ContentBlock, NavigationBinding, OutputLevel, PageSnapshot, Provenance,
    SnapshotError, TableRow, TruncationInfo, TruncationReason, VERIFIED_BY_BROWSER_PROCESS,
};
