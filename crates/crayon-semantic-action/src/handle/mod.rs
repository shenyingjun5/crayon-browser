//! Action handle contracts (ACT-03).
//!
//! An action handle is the short-lived, single-use `action_id` through
//! which a verified semantic action offer becomes executable. The registry
//! here owns handle state only; execution wiring belongs to ACT-07. Every
//! handle is bound to one target node, one tab, one page generation, one
//! profile scope, one TTL window and one one-time nonce, and dies on the
//! first mismatch — navigation, profile switch, TTL, replay or consumption.

mod data;
mod id;
mod registry;

pub use data::{
    ActionHandle, ActionHandleDescriptor, HandleIssueError, HandleNonce, ProfileScope,
    ProfileScopeError, MAX_HANDLE_TTL_MS,
};
pub use id::{ActionHandleId, HandleIdError};
pub use registry::{ConsumeOutcome, HandleRegistry, IssueOutcome, Resolution, MAX_ACTIVE_HANDLES};
