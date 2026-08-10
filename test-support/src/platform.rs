//! `PlatformFake`: deterministic platform capability fixtures and an
//! in-memory secure-store double with failure injection. Capture/codec/
//! network/lifecycle/update surface here is capability data only — real
//! platform adapters land in the PLT roadmap.

use crayon_domain::{
    BrowserEngineKind, LocalDiscoveryKind, PlatformCapabilities, ProtectedSurfaceKind,
    SecureStoreKind,
};
use std::collections::HashMap;
use std::sync::Mutex;

/// Platform capability fixtures per technical design §4.1/§4.2.
pub struct PlatformFake;

impl PlatformFake {
    /// Full desktop CEF capability set (Windows/macOS/Linux fixture).
    #[must_use]
    pub const fn cef_desktop() -> PlatformCapabilities {
        PlatformCapabilities::new(
            BrowserEngineKind::Cef,
            true,
            true,
            true,
            LocalDiscoveryKind::MdnsUdp,
            SecureStoreKind::OsNative,
            ProtectedSurfaceKind::Blocked,
        )
    }

    /// Reduced ArkWeb fixture (HarmonyOS preview: no tab capture, no system
    /// audio, no local discovery until device verification lands).
    #[must_use]
    pub const fn arkweb_reduced() -> PlatformCapabilities {
        PlatformCapabilities::new(
            BrowserEngineKind::ArkWeb,
            false,
            false,
            false,
            LocalDiscoveryKind::Unavailable,
            SecureStoreKind::Unavailable,
            ProtectedSurfaceKind::Blocked,
        )
    }
}

/// Secure-store failure modes for cleanup/rollback tests.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SecureStoreError {
    Unavailable,
    KeyNotFound,
    CapacityExceeded,
}

struct StoreState {
    entries: HashMap<String, Vec<u8>>,
    fail_next_write: Option<SecureStoreError>,
}

/// Maximum stored entries (bounded map rule).
const MAX_ENTRIES: usize = 128;
/// Maximum secret size in bytes (bounded value rule).
const MAX_VALUE_BYTES: usize = 4096;

/// In-memory secure-store double. Behavioural contract: put/get/delete,
/// overwrite replaces, delete of a missing key reports `KeyNotFound`,
/// injected write failures are consumed once.
pub struct SecureStoreFake {
    state: Mutex<StoreState>,
}

impl SecureStoreFake {
    #[must_use]
    pub fn new() -> Self {
        Self {
            state: Mutex::new(StoreState {
                entries: HashMap::new(),
                fail_next_write: None,
            }),
        }
    }

    pub fn put(&self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        let mut state = self.state.lock().unwrap();
        if let Some(error) = state.fail_next_write.take() {
            return Err(error);
        }
        if value.len() > MAX_VALUE_BYTES {
            return Err(SecureStoreError::CapacityExceeded);
        }
        if !state.entries.contains_key(key) && state.entries.len() >= MAX_ENTRIES {
            return Err(SecureStoreError::CapacityExceeded);
        }
        state.entries.insert(key.to_string(), value.to_vec());
        Ok(())
    }

    #[must_use]
    pub fn get(&self, key: &str) -> Option<Vec<u8>> {
        self.state.lock().unwrap().entries.get(key).cloned()
    }

    pub fn delete(&self, key: &str) -> Result<(), SecureStoreError> {
        let mut state = self.state.lock().unwrap();
        match state.entries.remove(key) {
            Some(_) => Ok(()),
            None => Err(SecureStoreError::KeyNotFound),
        }
    }

    /// Makes the next `put` fail with the given error (consumed once).
    pub fn fail_next_write(&self, error: SecureStoreError) {
        self.state.lock().unwrap().fail_next_write = Some(error);
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.state.lock().unwrap().entries.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

impl Default for SecureStoreFake {
    fn default() -> Self {
        Self::new()
    }
}
