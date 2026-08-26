//! macOS platform adapters for the PLT-01 interface contracts (PLT-M04).
//!
//! Slice plan (see `docs/plans/desktop-platform-adapters-roadmap.md`),
//! mirroring PLT-W04:
//! - M04a: Keychain-backed secure store + macOS capability document
//!   (this slice).
//! - M04b: local-network observation and power/session lifecycle.
//! - M04c: current-user UDS agent endpoint.
//! - M04d: update-flow driver (unavailable until QAR-09) and external
//!   client handoff.
//!
//! Surfaces not yet delivered are reported as unavailable in the
//! capability document rather than claimed.  The crate is empty on
//! non-macOS targets; workspace builds stay green elsewhere.
#![cfg(target_os = "macos")]

pub mod capabilities;
pub mod event_relay;
pub mod external_client_handoff;
pub mod ffi;
pub mod lifecycle;
pub mod local_agent_ipc;
pub mod local_network;
pub mod secure_store;
pub mod update;
