//! Sole product boundary over the pinned Cast-SDK sender source.
//!
//! Only this crate may depend on `cast-sender-*` packages from the locked
//! `third_party/cast-sdk` submodule (enforced by repo guard RG-005/RG-008).
//!
//! - SDK-02 established the pinned dependency edge and a link-time smoke test.
//! - SDK-03 freezes the product-facing contract: the `CastFacade` trait,
//!   strong DTOs and the stable `CastError` mapping (CS-008). No
//!   `cast_sender_*` type appears in any public signature.
//! - SDK-05 wires the real `SenderCommandService` behind the trait
//!   (`SenderCastFacade`): lifecycle/thread/callback encapsulation,
//!   session-event bridging and fail-closed fencing. The deterministic fake
//!   (SDK-04) lives in `test-support`.
//! - SDK-08 adds receiver capability synthesis and caching
//!   (`ReceiverCapabilityCache`): conservative assessment ->
//!   `ReceiverCapabilities` mapping (fail closed), TTL/epoch invalidation
//!   (CS-004).
//! - SDK-09 adds delivery execution (`deliver`): a policy-planned
//!   Direct/HLS/Relay URL becomes exactly one facade `cast_media` call with
//!   a stale-plan guard; external-client handoff stays unexpressible
//!   (CS-005).
//!
//! Browser, UI, and media crates must never see SDK-internal types.

mod capability;
mod delivery;
mod dto;
mod error;
mod facade;
mod service;

pub use capability::{
    synthesize_receiver_capabilities, CapabilityCacheConfig, ReceiverCapabilityCache,
    DEFAULT_ASSESSMENT_TTL, MAX_CACHED_DEVICES,
};
pub use delivery::{deliver, DeliveryRoute, PlannedDelivery};
pub use dto::{
    AssessmentStatus, CastCode, CastMediaKind, CastMediaRequest, CastMediaUrl, CastPlaybackState,
    CastSessionPhase, CastSessionRef, CastSessionSnapshot, CastTerminalReason, DeliveryProtocol,
    DeviceState, DiscoveredDevice, PlaybackPosition, ReceiverAssessment, Volume,
};
pub use error::{CastError, SenderErrorKind};
pub use facade::{CastFacade, CastSessionListener, CastSessionSubscription};
pub use service::{SenderCastFacade, SenderCastFacadeConfig};
