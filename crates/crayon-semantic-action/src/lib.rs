//! Semantic action contracts (ACT).
//!
//! The `detail` module freezes the three bounded output profiles of the
//! frozen v1 semantic map: `compact`, `standard` and `internal_full`. Only
//! `compact`/`standard` leave the Browser process; `internal_full` is the
//! bounded internal profile consumed by engine-side semantic tasks and is
//! never equivalent to raw DOM. Every profile projects the same
//! platform-independent `crayon-domain` vocabulary — no selectors, no raw
//! HTML, no CDP, no field values, no credentials — and every collection is
//! fenced by named budgets that report truncation instead of failing open.

mod detail;

pub use detail::{
    render_compact, render_internal_full, render_standard, CompactAction, CompactMap, CompactNode,
    DetailBudget, DetailProfile, InternalFullMap, SemanticNodeAnnotation, MAX_COMPACT_ACTIONS,
    MAX_COMPACT_NODES, MAX_STANDARD_ACTIONS, MAX_STANDARD_NODES,
};
