//! Profile model: strong identity types, random directory mapping and the
//! lifecycle state machine owned by `ProfileRegistry`.
//!
//! Invariants:
//! - Profile paths are derived only from the random directory ID, never from
//!   the profile ID or any user-chosen name.
//! - Lifecycle transitions are a closed set; illegal transitions (including
//!   repeated closes) are stable rejections.
//! - The registry performs no I/O; path composition is pure.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::error::Error;
use std::fmt::{Display, Formatter};
use std::path::{Path, PathBuf};

/// Maximum number of profiles held by one registry.
pub const MAX_PROFILES: usize = 64;

/// Maximum accepted length of a profile identifier, in bytes.
const MAX_PROFILE_ID_LEN: usize = 256;

/// Profile identifier validation failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProfileIdError {
    Empty,
    TooLong,
}

impl Display for ProfileIdError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::Empty => "profile id must not be empty",
            Self::TooLong => "profile id exceeds the maximum length",
        };
        formatter.write_str(message)
    }
}

impl Error for ProfileIdError {}

/// Validated profile identifier.  Mirrors the CEF-04 validator semantics:
/// non-empty, at most 256 bytes, UTF-8 (guaranteed by `str`).
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ProfileId(String);

impl ProfileId {
    /// Creates a validated profile identifier.
    pub fn new(value: &str) -> Result<Self, ProfileIdError> {
        if value.is_empty() {
            return Err(ProfileIdError::Empty);
        }
        if value.len() > MAX_PROFILE_ID_LEN {
            return Err(ProfileIdError::TooLong);
        }
        Ok(Self(value.to_owned()))
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Display for ProfileId {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.0)
    }
}

/// Closed set of profile types.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProfileType {
    /// Regular persistent profile.
    Regular,
    /// Ephemeral profile; its stores are never persisted and are cleaned up
    /// when the last window closes (cleanup owned by PRV-02).
    Incognito,
}

impl ProfileType {
    /// Reports whether the profile's stores must stay ephemeral.
    #[must_use]
    pub const fn is_ephemeral(self) -> bool {
        matches!(self, Self::Incognito)
    }
}

/// Operating-system entropy source failure.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EntropyError;

impl Display for EntropyError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("operating system entropy source unavailable")
    }
}

impl Error for EntropyError {}

/// Random 128-bit directory identifier, hex-encoded as the sole path
/// component for on-disk profile data.
#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct DirectoryId([u8; 16]);

impl DirectoryId {
    /// Generates a cryptographically random directory identifier.
    pub fn generate() -> Result<Self, EntropyError> {
        let mut bytes = [0_u8; 16];
        getrandom::fill(&mut bytes).map_err(|_| EntropyError)?;
        Ok(Self(bytes))
    }

    /// Deterministically builds an identifier from raw bytes.  Used by tests
    /// and by future persistence reload; not a way to derive IDs from names.
    #[must_use]
    pub const fn from_bytes(bytes: [u8; 16]) -> Self {
        Self(bytes)
    }

    /// Returns the 32-character lowercase hexadecimal encoding used as the
    /// directory name.
    #[must_use]
    pub fn to_hex(self) -> String {
        let mut hex = String::with_capacity(32);
        for byte in self.0 {
            hex.push(char::from_digit(u32::from(byte >> 4), 16).unwrap_or('0'));
            hex.push(char::from_digit(u32::from(byte & 0x0F), 16).unwrap_or('0'));
        }
        hex
    }
}

impl Display for DirectoryId {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.to_hex())
    }
}

/// Lifecycle state of a registered profile.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProfileLifecycle {
    Active,
    /// Close has begun; business operations are already rejected.
    Closing,
    /// Fully closed; the record may now be removed.
    Closed,
}

/// One registered profile.
#[derive(Clone, Debug)]
pub struct Profile {
    id: ProfileId,
    profile_type: ProfileType,
    directory_id: DirectoryId,
    lifecycle: ProfileLifecycle,
}

impl Profile {
    #[must_use]
    pub fn id(&self) -> &ProfileId {
        &self.id
    }

    #[must_use]
    pub const fn profile_type(&self) -> ProfileType {
        self.profile_type
    }

    #[must_use]
    pub const fn directory_id(&self) -> DirectoryId {
        self.directory_id
    }

    #[must_use]
    pub const fn lifecycle(&self) -> ProfileLifecycle {
        self.lifecycle
    }

    #[must_use]
    pub const fn is_active(&self) -> bool {
        matches!(self.lifecycle, ProfileLifecycle::Active)
    }
}

/// Registry command failure.  Variants are stable and carry no internal
/// detail.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ProfileError {
    InvalidId(ProfileIdError),
    DuplicateId,
    /// Another profile already owns this directory ID; directory sharing
    /// would break profile isolation.
    DirectoryIdInUse,
    UnknownId,
    /// The current lifecycle state does not allow the requested transition.
    IllegalState,
    Capacity,
    Entropy(EntropyError),
}

impl Display for ProfileError {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        let message = match self {
            Self::InvalidId(error) => return Display::fmt(error, formatter),
            Self::DuplicateId => "profile id is already registered",
            Self::DirectoryIdInUse => "directory id is already owned by another profile",
            Self::UnknownId => "profile id is not registered",
            Self::IllegalState => "profile lifecycle state rejects the operation",
            Self::Capacity => "profile registry capacity reached",
            Self::Entropy(error) => return Display::fmt(error, formatter),
        };
        formatter.write_str(message)
    }
}

impl Error for ProfileError {}

/// Platform-neutral owner of the profile set.
///
/// Composes on-disk paths as `root.join(directory_id.to_hex())`; the profile
/// ID never appears in any path.  No directories are created or deleted here.
pub struct ProfileRegistry {
    root: PathBuf,
    profiles: BTreeMap<ProfileId, Profile>,
}

impl ProfileRegistry {
    /// Creates a registry rooted at `root`.  The root must be absolute;
    /// deeper validation (symlink/reparse protection) belongs to PRV-04.
    pub fn new(root: PathBuf) -> Result<Self, ProfileError> {
        if !root.is_absolute() {
            return Err(ProfileError::IllegalState);
        }
        Ok(Self {
            root,
            profiles: BTreeMap::new(),
        })
    }

    #[must_use]
    pub fn root(&self) -> &Path {
        &self.root
    }

    #[must_use]
    pub fn profile_count(&self) -> usize {
        self.profiles.len()
    }

    /// Registers a profile with a freshly generated directory ID.
    pub fn create_profile(
        &mut self,
        id: &str,
        profile_type: ProfileType,
    ) -> Result<(), ProfileError> {
        let directory_id = DirectoryId::generate().map_err(ProfileError::Entropy)?;
        self.create_profile_with_directory(id, profile_type, directory_id)
    }

    /// Registers a profile with an explicitly supplied directory ID.  Used by
    /// deterministic tests and by future persistence reload.
    pub fn create_profile_with_directory(
        &mut self,
        id: &str,
        profile_type: ProfileType,
        directory_id: DirectoryId,
    ) -> Result<(), ProfileError> {
        let profile_id = ProfileId::new(id).map_err(ProfileError::InvalidId)?;
        if self.profiles.contains_key(&profile_id) {
            return Err(ProfileError::DuplicateId);
        }
        if self
            .profiles
            .values()
            .any(|profile| profile.directory_id == directory_id)
        {
            return Err(ProfileError::DirectoryIdInUse);
        }
        if self.profiles.len() >= MAX_PROFILES {
            return Err(ProfileError::Capacity);
        }
        let profile = Profile {
            id: profile_id.clone(),
            profile_type,
            directory_id,
            lifecycle: ProfileLifecycle::Active,
        };
        self.profiles.insert(profile_id, profile);
        Ok(())
    }

    /// Returns the profile for `id`, regardless of lifecycle state.
    #[must_use]
    pub fn find(&self, id: &ProfileId) -> Option<&Profile> {
        self.profiles.get(id)
    }

    /// Composes the on-disk path of a profile: `root/directory-id-hex`.
    /// Returns `None` for unknown IDs.  The path is pure data; this call
    /// performs no file-system access.
    pub fn profile_path(&self, id: &ProfileId) -> Option<PathBuf> {
        self.profiles
            .get(id)
            .map(|profile| self.root.join(profile.directory_id.to_hex()))
    }

    /// Begins closing a profile: `Active` -> `Closing`.  Repeated closes and
    /// closes of unknown profiles are stable rejections.
    pub fn begin_close(&mut self, id: &ProfileId) -> Result<(), ProfileError> {
        let profile = self.profiles.get_mut(id).ok_or(ProfileError::UnknownId)?;
        match profile.lifecycle {
            ProfileLifecycle::Active => {
                profile.lifecycle = ProfileLifecycle::Closing;
                Ok(())
            }
            ProfileLifecycle::Closing | ProfileLifecycle::Closed => Err(ProfileError::IllegalState),
        }
    }

    /// Completes closing: `Closing` -> `Closed`.
    pub fn finish_close(&mut self, id: &ProfileId) -> Result<(), ProfileError> {
        let profile = self.profiles.get_mut(id).ok_or(ProfileError::UnknownId)?;
        match profile.lifecycle {
            ProfileLifecycle::Closing => {
                profile.lifecycle = ProfileLifecycle::Closed;
                Ok(())
            }
            ProfileLifecycle::Active | ProfileLifecycle::Closed => Err(ProfileError::IllegalState),
        }
    }

    /// Removes the record of a fully closed profile.  Removing an active or
    /// closing profile is rejected so live state cannot silently vanish.
    pub fn remove(&mut self, id: &ProfileId) -> Result<(), ProfileError> {
        let lifecycle = self
            .profiles
            .get(id)
            .ok_or(ProfileError::UnknownId)?
            .lifecycle;
        if lifecycle != ProfileLifecycle::Closed {
            return Err(ProfileError::IllegalState);
        }
        self.profiles.remove(id);
        Ok(())
    }
}
