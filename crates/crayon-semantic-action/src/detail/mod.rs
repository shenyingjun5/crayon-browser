//! Bounded detail profiles over the frozen v1 semantic map (ACT-02, AC-002).
//!
//! A profile decides which fields of a [`crayon_domain::PageMap`] are
//! exposed and how many entries survive. The projection is pure and
//! deterministic: same map, same profile, same output. Nothing here can
//! carry raw DOM/HTML/CDP or pointers because the input and output types
//! only contain the frozen domain vocabulary.

mod compact;
mod internal;
mod profile;

pub use compact::{render_compact, CompactAction, CompactMap, CompactNode};
pub use compact::{MAX_COMPACT_ACTIONS, MAX_COMPACT_NODES};
pub use internal::{render_internal_full, InternalFullMap, SemanticNodeAnnotation};
pub use profile::{render_standard, DetailBudget, DetailProfile};
pub use profile::{MAX_STANDARD_ACTIONS, MAX_STANDARD_NODES};
