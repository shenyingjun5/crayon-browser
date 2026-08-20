//! Profile identity, random directory mapping, lifecycle state machine and
//! persistent storage transactions.
//!
//! The crate performs file-system access only inside `persistent`
//! transactions; `model` is pure.  Deeper path protection (symlink/reparse)
//! belongs to PRV-04 and secure storage to PRV-05.

mod ephemeral;
mod model;
mod persistent;

pub use ephemeral::{
    CleanupCategory, CleanupExecutor, CleanupOutcome, CleanupReport, EphemeralError,
    EphemeralSession, EphemeralState,
};
pub use model::{
    DirectoryId, EntropyError, Profile, ProfileError, ProfileId, ProfileIdError, ProfileLifecycle,
    ProfileRegistry, ProfileType, MAX_PROFILES,
};
pub use persistent::{DestroyOutcome, PersistentStore, PersistentStoreError};
