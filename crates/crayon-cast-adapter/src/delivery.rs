//! Delivery execution (SDK-09): turns one policy-planned Direct/HLS/Relay URL
//! into exactly one `CastFacade::cast_media` call (CS-005).
//!
//! Boundary and rules:
//! - the input is a `PlannedDelivery` produced by the runtime planner
//!   (MED-17/19) from a `Direct`/`Relay` policy decision;
//!   `ExternalClientHandoff` and `Reject` have no representation here and
//!   never create an SDK session;
//! - the executor only talks to the `CastFacade` trait — it never assembles
//!   SOAP/DLNA metadata, receiver control URLs or any descriptor, and the
//!   media URL is forwarded byte-for-byte (PL-002);
//! - Direct MP4 vs HLS is decided by the policy candidate protocol carried in
//!   `protocol`, never by URL suffix sniffing; a Relay delivery keeps the
//!   upstream protocol because the session relay serves MP4 or HLS as-is;
//! - pre-check (the official desktop app's `SessionExpired` semantics,
//!   expressed with the product error code): the planned device must still be
//!   the connected one — a mismatch, a device switch or no connection fails
//!   closed with `CastError::InvalidState` before any receiver traffic, and
//!   re-planning belongs to the runtime (SDK-12);
//! - one attempt only: a failure propagates as its stable `CastError`
//!   unchanged — no retry, no privilege upgrade, no silent fallback
//!   (PL-014); the single-step downgrade and resource-teardown orchestration
//!   belong to the runtime (SDK-12);
//! - a successful cast starts playing on the receiver by itself; the
//!   executor never issues a follow-up `play` (pinned SDK semantics);
//! - no capability re-check: the policy already consumed the SDK-08
//!   capability cache, where an unknown receiver fails closed into a
//!   handoff, so delivery never second-guesses that decision.

use crate::dto::{CastMediaRequest, CastMediaUrl, CastSessionRef, DeliveryProtocol};
use crate::error::CastError;
use crate::facade::CastFacade;
use crayon_domain::DeviceId;

/// How the receiver reaches the media of one planned delivery (CS-005).
///
/// The route is a product-level fact: it decides nothing on the SDK call
/// itself (the relay serves the same MP4/HLS protocol), but it keeps the
/// policy decision explicit so a relay delivery is auditable as such and an
/// external-client handoff stays unexpressible in this layer.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeliveryRoute {
    /// The receiver pulls the candidate URL directly.
    Direct,
    /// The receiver pulls through the session relay (opaque token URL).
    Relay,
}

/// One validated delivery order: a planned Direct/HLS/Relay URL bound to the
/// receiver the policy planned for.
///
/// Pure data, mirroring `CastMediaRequest`: it cannot express mirroring,
/// WebRTC or an external-client handoff (MED-19), and its `Debug` redacts
/// the media URL (signed query / relay token path).
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PlannedDelivery {
    /// Target device. Must still be the connected one at delivery time; a
    /// stale plan fails closed with `CastError::InvalidState`.
    device: DeviceId,
    route: DeliveryRoute,
    protocol: DeliveryProtocol,
    url: CastMediaUrl,
}

impl PlannedDelivery {
    #[must_use]
    pub const fn new(
        device: DeviceId,
        route: DeliveryRoute,
        protocol: DeliveryProtocol,
        url: CastMediaUrl,
    ) -> Self {
        Self {
            device,
            route,
            protocol,
            url,
        }
    }

    #[must_use]
    pub const fn device(&self) -> &DeviceId {
        &self.device
    }

    #[must_use]
    pub const fn route(&self) -> DeliveryRoute {
        self.route
    }

    #[must_use]
    pub const fn protocol(&self) -> DeliveryProtocol {
        self.protocol
    }

    #[must_use]
    pub const fn url(&self) -> &CastMediaUrl {
        &self.url
    }
}

/// Executes one planned delivery through the facade (CS-005).
///
/// Returns the fencing reference of the new session; any previous session is
/// replaced and reports `ReplacedByNewCast` through supervision. The call is
/// atomic from the caller's perspective: either one `cast_media` attempt
/// happened or none (stale plan), and the error is always a stable
/// `CastError` — never retried, upgraded or reinterpreted here.
pub fn deliver(
    facade: &dyn CastFacade,
    planned: &PlannedDelivery,
) -> Result<CastSessionRef, CastError> {
    // Stale-plan guard (SessionExpired semantics): the plan was made for a
    // specific receiver; if that receiver is no longer the connected one the
    // plan is void. Fail closed before any receiver traffic — the facade
    // repeats this check, the executor owns the product-level semantics.
    if facade.connected_device().as_ref() != Some(planned.device()) {
        return Err(CastError::InvalidState);
    }
    let request = CastMediaRequest::new(
        planned.device().clone(),
        planned.protocol(),
        planned.url().clone(),
    );
    facade.cast_media(&request)
}

#[cfg(test)]
#[path = "delivery_tests.rs"]
mod tests;
