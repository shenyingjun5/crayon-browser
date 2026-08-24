//! Capability Hub (HUB): unified description, deterministic registration
//! and explainable routing of built-in capabilities, personal Site Skills
//! and approved partner packages.
//!
//! This crate currently hosts the HUB-01 registry.  Router, policy,
//! fallback and the outbound partner connector belong to later HUB tasks.

pub mod builtin;
pub mod fallback;
pub mod policy;
pub mod registry;
pub mod router;
