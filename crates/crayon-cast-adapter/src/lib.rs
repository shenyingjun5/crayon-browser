//! Sole product boundary over the pinned Cast-SDK sender source.
//!
//! Only this crate may depend on `cast-sender-*` packages from the locked
//! `third_party/cast-sdk` submodule (enforced by repo guard RG-005/RG-008).
//! SDK-02 establishes the pinned dependency edge and a link-time smoke test;
//! the product-facing `CastFacade` trait, DTOs, and error mapping arrive with
//! SDK-03. Browser, UI, and media crates must never see SDK-internal types.
