//! Path guard: absolute root verification, symlink/reparse escape
//! protection and bounded compensating cleanup.
//!
//! Safety rules:
//! - The guard anchors on the canonical form of an absolute, existing
//!   directory root.
//! - Verified relative paths never contain parent references, absolute or
//!   prefix components, and stay within depth/length bounds.
//! - Every path component is checked with `symlink_metadata`; a symlink or
//!   reparse point anywhere under the root fails closed and the operation
//!   leaves every target untouched.
//! - Errors never carry paths or user data.
//!
//! Residual risk: verification and the subsequent removal are two system
//! calls, so a determined local attacker with write access inside the root
//! could race them (TOCTOU).  `std` offers no openat2-style protection;
//! closing that window belongs to a future platform-hardening task.

use std::error::Error;
use std::fmt::{Display, Formatter};
use std::fs;
use std::io;
use std::path::{Component, Path, PathBuf};

/// Maximum component depth of a verified relative path.
const MAX_GUARD_DEPTH: usize = 4;

/// Maximum byte length of a verified relative path.
const MAX_GUARD_PATH_LEN: usize = 256;

/// Maximum number of stale staging entries processed per cleanup call.
pub const MAX_CLEANUP_PER_CALL: usize = 16;

/// Suffix of directories staged for resumed deletion.
pub const STAGING_SUFFIX: &str = ".deleting-0";

/// `FILE_ATTRIBUTE_REPARSE_POINT`; covers symlinks, junctions and mount
/// points on Windows.
#[cfg(windows)]
const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;

/// Path-guard failure.  Variants are stable and never carry paths or user
/// data.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PathGuardError {
    /// The root is relative, missing, not a directory or cannot be
    /// canonicalised.
    RootInvalid,
    /// The relative path is empty, absolute, contains parent references or
    /// prefix components, or exceeds depth/length bounds.
    InvalidRelative,
    /// A path component is a symlink or reparse point (escape attempt).
    EscapeDetected,
    /// Underlying I/O failure with no further detail exposed.
    Io,
}

impl Display for PathGuardError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::RootInvalid => "profile root is not an absolute existing directory",
            Self::InvalidRelative => "relative path violates shape or bounds",
            Self::EscapeDetected => "path component is a symlink or reparse point",
            Self::Io => "file-system operation failed",
        };
        formatter.write_str(message)
    }
}

impl Error for PathGuardError {}

impl From<io::Error> for PathGuardError {
    fn from(_: io::Error) -> Self {
        Self::Io
    }
}

/// Owner of path verification and guarded removal under one root.
pub struct PathGuard {
    root: PathBuf,
}

impl PathGuard {
    /// Verifies the root is an absolute existing directory and anchors on
    /// its canonical form.
    pub fn new(root: &Path) -> Result<Self, PathGuardError> {
        if !root.is_absolute() {
            return Err(PathGuardError::RootInvalid);
        }
        let canonical = fs::canonicalize(root).map_err(|_| PathGuardError::RootInvalid)?;
        if !canonical.is_dir() {
            return Err(PathGuardError::RootInvalid);
        }
        Ok(Self { root: canonical })
    }

    /// The canonical root the guard is anchored on.
    #[must_use]
    pub fn root(&self) -> &Path {
        &self.root
    }

    /// Verifies that `relative` resolves strictly inside the root with no
    /// symlink or reparse component, and returns the absolute path.
    /// Every component, including the final one, must exist.
    pub fn verify_inside(&self, relative: &Path) -> Result<PathBuf, PathGuardError> {
        if relative.as_os_str().len() > MAX_GUARD_PATH_LEN {
            return Err(PathGuardError::InvalidRelative);
        }
        // Shape pass first: every component must be a plain name and the
        // depth is bounded before any file-system access happens.
        let mut parts = Vec::new();
        for component in relative.components() {
            let Component::Normal(part) = component else {
                // Absolute, root-dir, prefix, parent or current-dir pieces
                // are never valid inside a managed root.
                return Err(PathGuardError::InvalidRelative);
            };
            parts.push(part);
            if parts.len() > MAX_GUARD_DEPTH {
                return Err(PathGuardError::InvalidRelative);
            }
        }
        if parts.is_empty() {
            // An empty relative would resolve to the root itself; removing
            // the root is never a valid operation.
            return Err(PathGuardError::InvalidRelative);
        }
        // Guard pass: no component may be a symlink or reparse point.
        let mut current = self.root.clone();
        for part in parts {
            current.push(part);
            let metadata = fs::symlink_metadata(&current)?;
            if is_reparse(&metadata) {
                return Err(PathGuardError::EscapeDetected);
            }
        }
        Ok(current)
    }

    /// Removes the verified directory tree at `relative`.  When the guard
    /// rejects the path, nothing is modified.
    pub fn remove_tree(&self, relative: &Path) -> Result<(), PathGuardError> {
        let path = self.verify_inside(relative)?;
        fs::remove_dir_all(path)?;
        Ok(())
    }

    /// Startup compensating cleanup of stale staging directories directly
    /// under the root.  At most `max` entries are processed; entries that
    /// fail verification are skipped (never followed) and counted together
    /// with unprocessed entries as the returned remaining count.
    pub fn cleanup_staging(&self, max: usize) -> Result<usize, PathGuardError> {
        let mut remaining = 0_usize;
        let mut processed = 0_usize;
        for entry in fs::read_dir(&self.root)? {
            let entry = entry?;
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if !name.ends_with(STAGING_SUFFIX) {
                continue;
            }
            if processed >= max {
                remaining += 1;
                continue;
            }
            processed += 1;
            let metadata = fs::symlink_metadata(entry.path())?;
            if is_reparse(&metadata) {
                // Escape construction: never followed, never removed.
                remaining += 1;
                continue;
            }
            if fs::remove_dir_all(entry.path()).is_err() {
                remaining += 1;
            }
        }
        Ok(remaining)
    }
}

/// Reports whether the metadata describes a symlink (Unix) or any reparse
/// point (Windows: symlink, junction, mount point).
fn is_reparse(metadata: &fs::Metadata) -> bool {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0
    }
    #[cfg(not(windows))]
    {
        metadata.file_type().is_symlink()
    }
}
