//! Persistent profile storage transactions.
//!
//! Creation and destruction of a profile's on-disk space are transactional:
//! creation writes an ownership marker atomically, destruction renames the
//! directory to a `*.deleting-*` staging name before recursive removal so a
//! partial failure can be resumed by [`PersistentStore::retry_pending_destroys`].
//!
//! Safety rules:
//! - A directory is destroyed only after its marker matches the directory ID
//!   registered for the profile and the profile lifecycle is `Closed`.
//! - Operations never follow out-of-root targets; deeper symlink/reparse
//!   protection belongs to PRV-04.
//! - Error values never carry paths or user data.

use crate::model::{DirectoryId, ProfileLifecycle, ProfileRegistry, ProfileType};
use std::error::Error;
use std::fmt::{Display, Formatter};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// Marker file name inside every managed profile directory.
const MARKER_FILE_NAME: &str = ".crayon-profile";

/// Marker schema version written into the marker file.
const MARKER_SCHEMA_VERSION: u32 = 1;

/// Maximum number of stale `*.deleting-*` directories resumed per call.
const MAX_RESUME_PER_CALL: usize = 16;

/// Persistent-store transaction failure.  Variants are stable and never
/// carry paths or user data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PersistentStoreError {
    UnknownProfile,
    /// Incognito profiles have no persistent space.
    EphemeralProfile,
    /// The profile lifecycle does not allow the operation.
    IllegalState,
    /// The on-disk marker is missing, corrupt or belongs to another
    /// directory ID; the directory is left untouched (fail closed).
    OwnershipMismatch,
    /// Too many stale deletions pending; resume was bounded.
    ResumeCapacity,
    /// Underlying I/O failure with no further detail exposed.
    Io,
}

impl Display for PersistentStoreError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::UnknownProfile => "profile id is not registered",
            Self::EphemeralProfile => "ephemeral profiles have no persistent space",
            Self::IllegalState => "profile lifecycle state rejects the operation",
            Self::OwnershipMismatch => {
                "on-disk marker does not match the registered profile directory"
            }
            Self::ResumeCapacity => "too many stale deletions pending resume",
            Self::Io => "storage operation failed",
        };
        formatter.write_str(message)
    }
}

impl Error for PersistentStoreError {}

impl From<io::Error> for PersistentStoreError {
    fn from(_: io::Error) -> Self {
        Self::Io
    }
}

/// Outcome of a destroy transaction.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DestroyOutcome {
    /// The directory was fully removed in this call.
    Removed,
    /// The directory was staged for deletion but recursive removal failed;
    /// a later `retry_pending_destroys` call can finish it.
    StagedForResume,
}

/// Transactional owner of persistent profile directories under the
/// registry root.
pub struct PersistentStore<'a> {
    registry: &'a ProfileRegistry,
}

impl<'a> PersistentStore<'a> {
    #[must_use]
    pub fn new(registry: &'a ProfileRegistry) -> Self {
        Self { registry }
    }

    /// Creates the profile's directory and ownership marker.
    ///
    /// Idempotent: an existing directory with a valid marker is success.
    /// On marker-write failure the directory is removed best-effort so a
    /// retry starts from a clean state.
    pub fn create_space(
        &self,
        profile_id: &crate::ProfileId,
    ) -> Result<PathBuf, PersistentStoreError> {
        let profile = self.registered_regular_profile(profile_id)?;
        let path = self
            .registry
            .profile_path(profile_id)
            .ok_or(PersistentStoreError::UnknownProfile)?;
        if path.exists() {
            return self
                .verify_marker(&path, profile.directory_id())
                .map(|()| path);
        }
        fs::create_dir(&path)?;
        if let Err(error) = self.write_marker_atomic(&path, profile.directory_id()) {
            let _ = fs::remove_dir_all(&path);
            return Err(error);
        }
        Ok(path)
    }

    /// Destroys the profile's directory after ownership and lifecycle
    /// verification.  The directory is first renamed to a staging name so a
    /// failed recursive removal can be resumed later.
    pub fn destroy_space(
        &self,
        profile_id: &crate::ProfileId,
    ) -> Result<DestroyOutcome, PersistentStoreError> {
        let profile = self.registered_regular_profile(profile_id)?;
        if profile.lifecycle() != ProfileLifecycle::Closed {
            return Err(PersistentStoreError::IllegalState);
        }
        let path = self
            .registry
            .profile_path(profile_id)
            .ok_or(PersistentStoreError::UnknownProfile)?;
        if !path.exists() {
            return Ok(DestroyOutcome::Removed);
        }
        self.verify_marker(&path, profile.directory_id())?;
        let staging = staging_path(self.registry.root(), profile.directory_id());
        if staging.exists() {
            let _ = fs::remove_dir_all(&staging);
        }
        fs::rename(&path, &staging)?;
        match fs::remove_dir_all(&staging) {
            Ok(()) => Ok(DestroyOutcome::Removed),
            Err(_) => Ok(DestroyOutcome::StagedForResume),
        }
    }

    /// Resumes stale `*.deleting-*` directories directly under the root.
    /// Bounded per call; returns the number of directories still pending.
    pub fn retry_pending_destroys(&self) -> Result<usize, PersistentStoreError> {
        let mut pending = staging_entries(self.registry.root())?;
        let resumed = pending.len().saturating_sub(MAX_RESUME_PER_CALL);
        pending.truncate(MAX_RESUME_PER_CALL);
        for entry in pending {
            let _ = fs::remove_dir_all(entry);
        }
        Ok(staging_entries(self.registry.root())?.len().max(resumed))
    }

    fn registered_regular_profile(
        &self,
        profile_id: &crate::ProfileId,
    ) -> Result<crate::Profile, PersistentStoreError> {
        let profile = self
            .registry
            .find(profile_id)
            .ok_or(PersistentStoreError::UnknownProfile)?;
        if profile.profile_type() != ProfileType::Regular {
            return Err(PersistentStoreError::EphemeralProfile);
        }
        Ok(profile.clone())
    }

    fn verify_marker(
        &self,
        path: &Path,
        expected: DirectoryId,
    ) -> Result<(), PersistentStoreError> {
        let marker = path.join(MARKER_FILE_NAME);
        let content =
            fs::read_to_string(marker).map_err(|_| PersistentStoreError::OwnershipMismatch)?;
        if content == marker_content(expected) {
            Ok(())
        } else {
            Err(PersistentStoreError::OwnershipMismatch)
        }
    }

    fn write_marker_atomic(
        &self,
        path: &Path,
        directory_id: DirectoryId,
    ) -> Result<(), PersistentStoreError> {
        let staging = path.join(format!("{MARKER_FILE_NAME}.tmp"));
        fs::write(&staging, marker_content(directory_id))?;
        fs::rename(&staging, path.join(MARKER_FILE_NAME))?;
        Ok(())
    }
}

fn marker_content(directory_id: DirectoryId) -> String {
    format!(
        "schema={MARKER_SCHEMA_VERSION}\ndirectory={}\n",
        directory_id.to_hex()
    )
}

fn staging_path(root: &Path, directory_id: DirectoryId) -> PathBuf {
    root.join(format!("{}.deleting-0", directory_id.to_hex()))
}

fn staging_entries(root: &Path) -> Result<Vec<PathBuf>, PersistentStoreError> {
    let mut entries = Vec::new();
    for entry in fs::read_dir(root)? {
        let entry = entry?;
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name.ends_with(".deleting-0") {
            entries.push(entry.path());
        }
    }
    Ok(entries)
}
