//! Profile identity, random directory mapping, lifecycle state machine,
//! persistent storage transactions and path-guard escape protection.
//!
//! The crate performs file-system access only inside `persistent`
//! transactions and `path_guard`; `model` is pure.  Secure storage belongs
//! to PRV-05.

mod ephemeral;
mod model;
mod path_guard;
mod persistent;

pub use ephemeral::{
    CleanupCategory, CleanupExecutor, CleanupOutcome, CleanupReport, EphemeralError,
    EphemeralSession, EphemeralState,
};
pub use model::{
    DirectoryId, EntropyError, Profile, ProfileError, ProfileId, ProfileIdError, ProfileLifecycle,
    ProfileRegistry, ProfileType, MAX_PROFILES,
};
pub use path_guard::{PathGuard, PathGuardError, MAX_CLEANUP_PER_CALL, STAGING_SUFFIX};
pub use persistent::{DestroyOutcome, PersistentStore, PersistentStoreError};
