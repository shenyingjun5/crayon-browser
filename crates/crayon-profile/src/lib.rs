//! Profile identity, random directory mapping and lifecycle state machine.
//!
//! The model is platform-neutral and performs no file-system access.  Disk
//! creation/cleanup transactions belong to the `ephemeral`/`persistent`
//! modules (PRV-02/PRV-03); secure storage belongs to PRV-05.

mod model;

pub use model::{
    DirectoryId, EntropyError, Profile, ProfileError, ProfileId, ProfileIdError, ProfileLifecycle,
    ProfileRegistry, ProfileType, MAX_PROFILES,
};
