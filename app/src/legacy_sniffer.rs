//! Loader for the legacy observation script kept outside Rust production code.

pub(crate) const SNIFF_JS: &str = concat!(include_str!("scripts/legacy_sniffer.js"), "\n");
