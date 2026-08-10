//! Crayon session relay (formal product relay, MED-09+).
//!
//! Unlike the legacy arbitrary-URL proxy, this relay only exposes opaque
//! session/resource routes; upstream URLs, Referer/UA recipes and secrets
//! stay in Core memory and are revoked with the session.

pub mod network_guard;
pub mod router;
pub mod session;
pub mod vault;
