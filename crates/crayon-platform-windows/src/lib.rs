//! Windows platform adapters for the PLT-01 interface contracts (PLT-W04).
//!
//! Slice plan (see `docs/plans/desktop-platform-adapters-roadmap.md`):
//! - W04a: DPAPI-backed `SecureStore` and the Windows adapter capability
//!   document (this slice).
//! - W04b: local-network observation and power/session lifecycle events.
//! - W04c: current-user named-pipe endpoint (AG-012 peer identity facts).
//! - W04d: update-flow driver and external cast-client handoff.
//!
//! Surfaces not yet delivered are reported as unavailable in the
//! capability document rather than claimed.  The crate is empty on
//! non-Windows targets; workspace builds stay green on macOS CI.
#![cfg(windows)]

pub mod capabilities;
mod event_relay;
pub mod ffi;
pub mod lifecycle;
pub mod local_network;
pub mod secure_store;
