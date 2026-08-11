//! The product-side `CastFacade` contract (SDK-03).
//!
//! This is the only boundary the browser, runtime and (later) Agent read
//! paths use for LAN casting. It covers exactly the six capability groups of
//! the architecture contract: discovery, connection/cast code, capability
//! assessment, URL/HLS delivery, session-bound playback control, and session
//! supervision with generation fencing.
//!
//! Contract rules:
//! - no `cast_sender_*` type appears in any signature;
//! - methods are synchronous and reveal nothing about SDK threading; SDK-05
//!   owns the lifecycle/threading wrapper behind this trait;
//! - failures are the stable `CastError` enum (CS-008), never SDK messages;
//! - mirroring, WebRTC and external-client handoff are not expressible here
//!   (MED-19).

use crate::dto::{
    CastCode, CastMediaKind, CastMediaRequest, CastSessionRef, CastSessionSnapshot,
    DiscoveredDevice, PlaybackPosition, ReceiverAssessment, Volume,
};
use crate::error::CastError;
use crayon_domain::DeviceId;
use std::sync::Arc;

/// Receives session supervision events.
///
/// Blanket-implemented for closures. Events carry full fencing data; a
/// listener must drop a snapshot that does not `supersede` the last applied
/// one (old-generation events must never stop a newer session, CS-007).
pub trait CastSessionListener: Send + Sync {
    fn on_session_changed(&self, snapshot: CastSessionSnapshot);
}

impl<F> CastSessionListener for F
where
    F: Fn(CastSessionSnapshot) + Send + Sync,
{
    fn on_session_changed(&self, snapshot: CastSessionSnapshot) {
        self(snapshot);
    }
}

/// Ongoing session-event subscription. Dropping it unsubscribes and must be
/// idempotent (SDK-11 wires this onto the SDK subscription handle).
pub trait CastSessionSubscription: Send {}

/// Sole product boundary over the pinned Cast-SDK sender facade.
///
/// Object-safe on purpose: app-runtime and the SDK-04 fake are used through
/// `&dyn CastFacade` / `Arc<dyn CastFacade>`. Implementations must be safe to
/// call from any thread; all lifecycle methods must be idempotent.
pub trait CastFacade: Send + Sync {
    // -- Discovery (CS-001/CS-002) ----------------------------------------
    // Lifecycle is idempotent: repeated start/stop is not an error. UI and
    // Agent reads consume snapshots only; no IP or location ever crosses.
    //
    // Finalized snapshot semantics (SDK-06):
    // - the snapshot contains currently connectable receivers only: a device
    //   that aged out (stale/offline) or never resolved disappears from the
    //   list instead of showing a degraded entry, and reappears under the
    //   same stable `DeviceId` once it resolves again;
    // - stopping discovery never clears the snapshot — the last known
    //   connectable set stays readable until devices age out or the facade
    //   is restarted;
    // - one logical receiver appears exactly once under one stable
    //   `DeviceId`, even across same-name receivers, duplicate-UDN
    //   registrations and multi-interface/IP-change re-announces; the id
    //   never embeds an IP, so an address change keeps the identity;
    // - the snapshot has a deterministic total order (friendly name, then
    //   device id) so UI diffing never flickers;
    // - there is deliberately no incremental event channel: the pinned SDK
    //   publishes discovery deltas only inside its worker, and CS-001
    //   consumers poll the snapshot (SDK-06 review decision).

    /// Starts (or keeps running) LAN device discovery.
    fn start_discovery(&self) -> Result<(), CastError>;

    /// Stops discovery; a no-op when not running. The device snapshot is
    /// retained (see the discovery contract above).
    fn stop_discovery(&self) -> Result<(), CastError>;

    /// Re-queries immediately. Also starts discovery when it is not running
    /// (pinned SDK behaviour), so it never fails just because discovery was
    /// off.
    fn refresh_discovery(&self) -> Result<(), CastError>;

    /// Current device snapshot: connectable receivers only, stable
    /// `DeviceId`s (never IPs), deterministic order.
    fn list_devices(&self) -> Vec<DiscoveredDevice>;

    fn is_discovery_running(&self) -> bool;

    // -- Connection & cast code (CS-003) ----------------------------------
    // Stable state mapping (finalized in SDK-07):
    // - resolve: Ok(device) / `InvalidCastCode` (the SDK codec rejects the
    //   exact alphabet, range or checksum) / `DeviceNotFound` (a valid but
    //   unanswered or expired code) / `NetworkUnavailable` /
    //   `ReceiverUnreachable` (LAN failure). The product never reimplements
    //   the codec.
    // - cancel: the pinned SDK has no cooperative cancel on resolution; the
    //   call is bounded (per-route discovery timeout over the SDK's fixed
    //   candidate port set) and cancel is caller-side abandonment — a late
    //   result is simply discarded and a late success only registers the
    //   device like a fresh resolve, so no facade error ever surfaces for a
    //   cancelled resolve (SDK gap recorded in the roadmap).
    // - connect: idempotent for the same device, switches when another
    //   device is connected; `DeviceNotFound` when the device is absent from
    //   the current snapshot (including aged-out devices), `RouteLost` when
    //   it is visible but its validated route expired (re-discover first).
    // - disconnect: infallible and idempotent; reconnect afterwards is an
    //   ordinary fresh connect.

    /// Resolves a receiver by its six-character cast code.
    ///
    /// Stable outcomes: Ok(device) / `InvalidCastCode` (codec rejection) /
    /// `DeviceNotFound` (unanswered or expired code) / `NetworkUnavailable`
    /// or `ReceiverUnreachable` (LAN failure). The product never reimplements
    /// the codec.
    fn resolve_device_by_cast_code(&self, code: &CastCode) -> Result<DiscoveredDevice, CastError>;

    /// Connects to a discovered device. Idempotent for the same device and
    /// switches when another device is connected. `DeviceNotFound` when the
    /// device is absent from the snapshot (including aged-out devices);
    /// `RouteLost` when it is visible but its routes expired (re-discover
    /// first).
    fn connect(&self, device: &DeviceId) -> Result<(), CastError>;

    /// Disconnects the current device, if any. Infallible and idempotent:
    /// disconnecting with no active connection is a no-op, any active cast
    /// session is torn down through normal supervision, and a reconnect
    /// afterwards is an ordinary fresh `connect`.
    fn disconnect(&self);

    /// Currently connected device, if any.
    fn connected_device(&self) -> Option<DeviceId>;

    // -- Capability assessment (CS-004) -----------------------------------

    /// Assesses one media kind against a discovered device. The result is a
    /// point-in-time fact; `ReceiverCapabilityCache` (SDK-08) owns the
    /// conservative synthesis into `ReceiverCapabilities` and TTL/epoch
    /// caching on top of this call.
    fn assess_receiver(
        &self,
        device: &DeviceId,
        media: CastMediaKind,
    ) -> Result<ReceiverAssessment, CastError>;

    // -- Delivery (CS-005) -------------------------------------------------

    /// Starts casting one planned Direct/HLS/Relay URL to the connected
    /// device. The request device must equal `connected_device()`; the
    /// facade fails closed otherwise. Returns the fencing reference of the
    /// new session; any previous session is replaced and reports
    /// `ReplacedByNewCast` through supervision.
    fn cast_media(&self, request: &CastMediaRequest) -> Result<CastSessionRef, CastError>;

    // -- Session-bound playback control (CS-006) ---------------------------
    // Every method fences on the session reference: a stale generation is
    // rejected with `StaleSessionGeneration` and never reaches the receiver.

    fn play(&self, session: &CastSessionRef) -> Result<(), CastError>;

    fn pause(&self, session: &CastSessionRef) -> Result<(), CastError>;

    fn seek(&self, session: &CastSessionRef, position_seconds: u64) -> Result<(), CastError>;

    fn set_volume(&self, session: &CastSessionRef, volume: Volume) -> Result<(), CastError>;

    fn set_muted(&self, session: &CastSessionRef, muted: bool) -> Result<(), CastError>;

    /// Stops the session. Idempotent: an already-terminated session reports
    /// success; only a stale or foreign reference is an error.
    fn stop(&self, session: &CastSessionRef) -> Result<(), CastError>;

    /// Current playback position of the session (no track URI, ever).
    fn playback_position(&self, session: &CastSessionRef) -> Result<PlaybackPosition, CastError>;

    // -- Session supervision (CS-007) --------------------------------------

    /// Latest supervised session snapshot, if any.
    fn current_session(&self) -> Option<CastSessionSnapshot>;

    /// Subscribes to session events. With `notify_immediately`, the current
    /// snapshot (when present) is delivered once to the new listener.
    /// Dropping the returned subscription unsubscribes.
    fn subscribe_session_events(
        &self,
        listener: Arc<dyn CastSessionListener>,
        notify_immediately: bool,
    ) -> Box<dyn CastSessionSubscription>;
}
