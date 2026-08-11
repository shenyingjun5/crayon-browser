//! Receiver capability synthesis and caching (SDK-08, CS-004).
//!
//! The pinned SDK assesses one media type at a time (`assess_receiver`);
//! policy consumes a whole `ReceiverCapabilities`. This module owns the
//! conservative mapping between the two plus the TTL/epoch cache that keeps
//! policy golden inputs consistent with the latest assessment:
//!
//! - fail closed: only an explicit `Supported` assessment maps to `true`.
//!   `Risky`, `Unsupported`, `Unknown` and any assessment error are never
//!   presented as support (PL-013). The pinned SDK has no codec/resolution
//!   matrix (SDK-03 review finding), so `dash`, `h264`, `hevc`, `av1` and
//!   `max_height` are always `false`/`0` here; presenting them as supported
//!   would be guessing. Widening them needs a Cast-SDK capability API change
//!   (gap tracked by SDK-14/SDK-15);
//! - TTL: a cached entry is served for at most `assessment_ttl`; afterwards
//!   the next read re-assesses and a capability change is picked up;
//! - device epoch: every entry is bound to a per-device epoch. The discovery
//!   contract (SDK-06) gives one logical receiver a stable `DeviceId`, so an
//!   epoch bump is the only way to retire an assessment taken before a
//!   lifecycle event. The runtime calls `invalidate` on disconnect, device
//!   switch and route loss, and `invalidate_all` on facade restart (wired by
//!   SDK-12). An in-flight refresh that races an invalidation is discarded
//!   by an epoch compare-and-set, so a pre-invalidation assessment can never
//!   be cached over a newer epoch;
//! - continuous presence: an entry is served only while the device stays in
//!   the discovery snapshot. A receiver that aged out and reappears under
//!   the same stable id after a read observed its absence is re-assessed
//!   (device replacement path, CS-002/CS-004);
//! - connection relationship (SDK-07 frozen semantics): an assessment does
//!   not depend on the connection, and a reconnect is an ordinary fresh
//!   connect, so the cache tracks no connection state itself — the runtime
//!   owns signalling lifecycle events through `invalidate`/`invalidate_all`.
//!
//! Failure semantics: a refresh that fails caches nothing and propagates the
//! error; a stale entry is never served after expiry or invalidation, and a
//! failed refresh never resurrects one.
//!
//! Concurrency rules (AGENTS §9): the single state mutex is held only for
//! map reads/writes — never across a facade call. Concurrent refreshes of
//! one device are benign (both assess; the last store wins with equally
//! fresh data). The cache is bounded to `MAX_CACHED_DEVICES` entries; making
//! room evicts expired entries first, then cached entries (losing a cache
//! hit is always safe), never tombstones left by invalidations; when only
//! tombstones remain the store is skipped and counted in `overflow_skips`.

use crate::dto::{AssessmentStatus, CastMediaKind};
use crate::error::CastError;
use crate::facade::CastFacade;
use crayon_domain::{DeviceId, ReceiverCapabilities};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// Default assessment TTL: long enough to avoid re-assessing on every cast
/// attempt, short enough that a receiver profile change is picked up well
/// within a browsing session. Tuning lives in `CapabilityCacheConfig`.
pub const DEFAULT_ASSESSMENT_TTL: Duration = Duration::from_secs(30);

/// Upper bound for cached device entries (bounded-cache rule, AGENTS §9).
/// One entry per distinct LAN receiver; 64 is far beyond any realistic LAN.
/// Public so behaviour tests can drive the eviction paths deterministically.
pub const MAX_CACHED_DEVICES: usize = 64;

/// Conservative synthesis of per-media-kind assessments into the policy
/// `ReceiverCapabilities` golden input (CS-004, PL-013).
///
/// `video` is the assessment for progressive MP4 (`CastMediaKind::Video`),
/// `hls` for HLS. Only `Supported` maps to `true`; every other status fails
/// closed. Codec flags and `max_height` stay `false`/`0` because the pinned
/// SDK reports no codec/resolution matrix.
#[must_use]
pub fn synthesize_receiver_capabilities(
    video: AssessmentStatus,
    hls: AssessmentStatus,
) -> ReceiverCapabilities {
    ReceiverCapabilities::new(
        video == AssessmentStatus::Supported,
        hls == AssessmentStatus::Supported,
        false,
        false,
        false,
        false,
        0,
    )
}

/// TTL/epoch cache configuration.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CapabilityCacheConfig {
    /// How long a synthesized entry is served before re-assessment.
    pub assessment_ttl: Duration,
}

impl Default for CapabilityCacheConfig {
    fn default() -> Self {
        Self {
            assessment_ttl: DEFAULT_ASSESSMENT_TTL,
        }
    }
}

/// One cached synthesis. `assessed_at` is a reading of the injected clock.
#[derive(Clone, Copy, Debug)]
struct CachedAssessment {
    capabilities: ReceiverCapabilities,
    assessed_at: Duration,
}

/// Per-device cache line: a monotonically bumped epoch plus the cached
/// assessment of that epoch (`None` is a tombstone left by an invalidation,
/// kept so an in-flight pre-invalidation refresh is discarded on store).
#[derive(Debug, Default)]
struct DeviceEntry {
    epoch: u64,
    assessment: Option<CachedAssessment>,
}

#[derive(Debug, Default)]
struct CacheState {
    devices: HashMap<DeviceId, DeviceEntry>,
    /// Bumped by `invalidate_all`; stores started before the bump are
    /// discarded even for devices that had no entry at the time.
    generation: u64,
    /// Store attempts skipped because the cache held only tombstones.
    overflow_skips: u64,
}

/// TTL/epoch-bound receiver capability cache over any `CastFacade`.
///
/// Works identically over the real `SenderCastFacade` and the SDK-04 fake:
/// both answer `assess_receiver` with the latest point-in-time fact, and the
/// cache owns freshness. Thread-safe; see the module doc for semantics.
pub struct ReceiverCapabilityCache {
    facade: Arc<dyn CastFacade>,
    config: CapabilityCacheConfig,
    /// Monotonic clock readings as durations. Tests inject a manual clock;
    /// production uses an `Instant` anchor (monotonic, process-local).
    now: Arc<dyn Fn() -> Duration + Send + Sync>,
    state: Mutex<CacheState>,
}

impl ReceiverCapabilityCache {
    /// Builds a cache on a monotonic `Instant`-anchored clock.
    #[must_use]
    pub fn new(facade: Arc<dyn CastFacade>, config: CapabilityCacheConfig) -> Self {
        let anchor = Instant::now();
        Self::with_clock(facade, config, Arc::new(move || anchor.elapsed()))
    }

    /// Builds a cache on an injected monotonic clock (deterministic tests).
    /// The clock must be cheap and must never re-enter the cache: it may run
    /// while the state lock is held (capacity eviction).
    #[must_use]
    pub fn with_clock(
        facade: Arc<dyn CastFacade>,
        config: CapabilityCacheConfig,
        now: Arc<dyn Fn() -> Duration + Send + Sync>,
    ) -> Self {
        Self {
            facade,
            config,
            now,
            state: Mutex::new(CacheState::default()),
        }
    }

    fn now(&self) -> Duration {
        (self.now)()
    }

    /// Current capabilities of a device, from cache or a fresh assessment.
    ///
    /// A cached entry is served only when it is younger than the TTL, the
    /// device epoch was not bumped since, and the device is still in the
    /// discovery snapshot. Otherwise both media kinds are re-assessed and
    /// conservatively synthesized. Assessment errors fail closed: nothing is
    /// cached, no stale entry is served, and the error propagates.
    ///
    /// The hit path costs one in-process snapshot read (`list_devices`) —
    /// acceptable for per-cast-attempt policy evaluation, not a hot path.
    pub fn capabilities(&self, device: &DeviceId) -> Result<ReceiverCapabilities, CastError> {
        if let Some(cached) = self.cached_capabilities(device) {
            return Ok(cached);
        }
        let token = self.freshness_token(device);
        let video = self
            .facade
            .assess_receiver(device, CastMediaKind::Video)?
            .status();
        let hls = self
            .facade
            .assess_receiver(device, CastMediaKind::Hls)?
            .status();
        let capabilities = synthesize_receiver_capabilities(video, hls);
        self.store(device, token, capabilities);
        Ok(capabilities)
    }

    /// Bumps the device epoch and drops its cached assessment.
    ///
    /// Call on disconnect, device switch and route loss (SDK-12 wiring). A
    /// refresh already in flight for this device is discarded when it tries
    /// to store, so a pre-invalidation assessment never outlives the event.
    pub fn invalidate(&self, device: &DeviceId) {
        let mut state = self.lock_state();
        if let Some(entry) = state.devices.get_mut(device) {
            entry.epoch += 1;
            entry.assessment = None;
            return;
        }
        // Tombstone for a not-yet-cached device: protects an in-flight
        // refresh of this device from storing across the invalidation. When
        // the map is full of tombstones no refresh of this device can store
        // either (`store` obeys the same bound), so nothing goes stale.
        if self.make_room(&mut state) {
            state.devices.insert(
                device.clone(),
                DeviceEntry {
                    epoch: 1,
                    assessment: None,
                },
            );
        }
    }

    /// Drops every cached assessment and bumps the global generation, so a
    /// refresh already in flight for any device is discarded on store. Call
    /// on facade restart (SDK-12 wiring).
    pub fn invalidate_all(&self) {
        let mut state = self.lock_state();
        state.generation += 1;
        state.devices.clear();
    }

    /// Valid cached capabilities for `device`, or `None`. An entry whose
    /// device left the discovery snapshot is invalidated on the spot (the
    /// receiver may be replaced under the same stable id before reappearing).
    fn cached_capabilities(&self, device: &DeviceId) -> Option<ReceiverCapabilities> {
        let now = self.now();
        let cached = {
            let state = self.lock_state();
            let entry = state.devices.get(device)?;
            let assessment = entry.assessment?;
            if now.saturating_sub(assessment.assessed_at) >= self.config.assessment_ttl {
                return None;
            }
            assessment.capabilities
        };
        // Presence check outside the lock: `list_devices` may take SDK locks.
        let present = self
            .facade
            .list_devices()
            .iter()
            .any(|listed| listed.device_id() == device);
        if present {
            Some(cached)
        } else {
            self.invalidate(device);
            None
        }
    }

    /// Freshness token read before a refresh: the global generation plus the
    /// device epoch. Compared again at store time.
    fn freshness_token(&self, device: &DeviceId) -> (u64, u64) {
        let state = self.lock_state();
        let epoch = state.devices.get(device).map_or(0, |entry| entry.epoch);
        (state.generation, epoch)
    }

    /// Stores a fresh synthesis, unless the global generation or the device
    /// epoch moved while the refresh was in flight (invalidated — the older
    /// assessment must not be cached over the newer epoch).
    fn store(&self, device: &DeviceId, token: (u64, u64), capabilities: ReceiverCapabilities) {
        let assessed_at = self.now();
        let mut state = self.lock_state();
        if state.generation != token.0 {
            return;
        }
        if !state.devices.contains_key(device) && !self.make_room(&mut state) {
            state.overflow_skips = state.overflow_skips.saturating_add(1);
            return;
        }
        let entry = state.devices.entry(device.clone()).or_default();
        if entry.epoch != token.1 {
            return;
        }
        entry.assessment = Some(CachedAssessment {
            capabilities,
            assessed_at,
        });
    }

    /// Frees one slot while the map is at capacity. Expired entries go
    /// first, then cached entries (losing a hit is safe; correctness is
    /// never-serving-stale, never retention). Tombstones are kept: they
    /// protect in-flight refreshes. Returns false when only tombstones
    /// remain.
    fn make_room(&self, state: &mut CacheState) -> bool {
        if state.devices.len() < MAX_CACHED_DEVICES {
            return true;
        }
        let now = self.now();
        let ttl = self.config.assessment_ttl;
        state.devices.retain(|_, entry| {
            entry
                .assessment
                .as_ref()
                .is_none_or(|assessment| now.saturating_sub(assessment.assessed_at) < ttl)
        });
        if state.devices.len() < MAX_CACHED_DEVICES {
            return true;
        }
        if let Some(key) = state
            .devices
            .iter()
            .find(|(_, entry)| entry.assessment.is_some())
            .map(|(key, _)| key.clone())
        {
            state.devices.remove(&key);
        }
        state.devices.len() < MAX_CACHED_DEVICES
    }

    fn lock_state(&self) -> std::sync::MutexGuard<'_, CacheState> {
        self.state.lock().unwrap_or_else(|error| error.into_inner())
    }
}

impl std::fmt::Debug for ReceiverCapabilityCache {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("ReceiverCapabilityCache")
            .field("config", &self.config)
            .finish_non_exhaustive()
    }
}

#[cfg(test)]
#[path = "capability_tests.rs"]
mod tests;
