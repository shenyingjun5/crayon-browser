//! Windows platform adapters for the PLT-01 interface contracts (PLT-W04).
//!
//! Slice plan (see `docs/plans/desktop-platform-adapters-roadmap.md`):
//! All four slices are delivered: W04a DPAPI secure store, W04b
//! local-network/lifecycle observation, W04c named-pipe endpoint, W04d
//! update-flow driver and client handoff.
//!
//! Surfaces not yet delivered are reported as unavailable in the
//! capability document rather than claimed.  The crate is empty on
//! non-Windows targets; workspace builds stay green on macOS CI.
#![cfg(windows)]

pub mod capabilities;
pub mod event_relay;
pub mod external_client_handoff;
pub mod ffi;
pub mod lifecycle;
pub mod local_agent_ipc;
pub mod local_network;
pub mod secure_store;
pub mod update;
