//! Sole product boundary over the pinned Cast-SDK sender source.
//!
//! Only this crate may depend on `cast-sender-*` packages from the locked
//! `third_party/cast-sdk` submodule (enforced by repo guard RG-005/RG-008).
//!
//! - SDK-02 established the pinned dependency edge and a link-time smoke test.
//! - SDK-03 freezes the product-facing contract: the `CastFacade` trait,
//!   strong DTOs and the stable `CastError` mapping (CS-008). No
//!   `cast_sender_*` type appears in any public signature, and no SDK wiring
//!   lives here yet — the fake (SDK-04) and the real `SenderCommandService`
//!   wrapper (SDK-05) implement the trait behind this boundary.
//!
//! Browser, UI, and media crates must never see SDK-internal types.

mod dto;
mod error;
mod facade;

pub use dto::{
    AssessmentStatus, CastCode, CastMediaKind, CastMediaRequest, CastMediaUrl, CastPlaybackState,
    CastSessionPhase, CastSessionRef, CastSessionSnapshot, CastTerminalReason, DeliveryProtocol,
    DeviceState, DiscoveredDevice, PlaybackPosition, ReceiverAssessment, Volume,
};
pub use error::{CastError, SenderErrorKind};
pub use facade::{CastFacade, CastSessionListener, CastSessionSubscription};
