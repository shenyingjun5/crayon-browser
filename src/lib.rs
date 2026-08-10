//! Crayon formal core and explicitly feature-gated legacy modules.
//!
//! The default `formal-product` build exposes no legacy extraction, relay,
//! sniffing, or CLI surface. Existing modules compile only with the mutually
//! exclusive `legacy-dev` feature until their safe capabilities are migrated.

#[cfg(all(feature = "formal-product", feature = "legacy-dev"))]
compile_error!("formal-product and legacy-dev are mutually exclusive build modes");

/// Stable formal runtime assembly API.
pub use crayon_app_runtime as app_runtime;
/// Pure, fail-closed cast planning gates.
pub use crayon_cast_policy as cast_policy;
/// Platform-independent product types.
pub use crayon_domain as domain;
/// Versioned browser/core transport types.
pub use crayon_ipc_schema as ipc_schema;
/// Browser observation facts without browser-engine types.
pub use crayon_media_observer as media_observer;
/// Platform-neutral media format and protection probes.
pub use crayon_media_probe as media_probe;

pub use crayon_app_runtime::RuntimeDescriptor;
pub use crayon_domain::{ProductIdentity, ProductIdentityError, ProductMode};
pub use crayon_ipc_schema::{Handshake, SchemaVersion};

#[cfg(feature = "legacy-dev")]
pub mod codec;
#[cfg(feature = "legacy-dev")]
pub mod drm;
#[cfg(feature = "legacy-dev")]
pub mod extract;
#[cfg(feature = "legacy-dev")]
pub mod probe;
#[cfg(feature = "legacy-dev")]
pub mod relay;

/// 默认桌面浏览器 UA。
pub const DEFAULT_UA: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

/// 等价于 JS 的 encodeURIComponent：除 `A-Za-z0-9-_.!~*'()` 外全部编码。
pub fn encode_url_component(s: &str) -> String {
    const SET: &percent_encoding::AsciiSet = &percent_encoding::CONTROLS
        .add(b' ')
        .add(b'"')
        .add(b'#')
        .add(b'$')
        .add(b'%')
        .add(b'&')
        .add(b'+')
        .add(b',')
        .add(b'/')
        .add(b':')
        .add(b';')
        .add(b'<')
        .add(b'=')
        .add(b'>')
        .add(b'?')
        .add(b'@')
        .add(b'[')
        .add(b'\\')
        .add(b']')
        .add(b'^')
        .add(b'`')
        .add(b'{')
        .add(b'|')
        .add(b'}');
    percent_encoding::utf8_percent_encode(s, SET).to_string()
}

#[cfg(test)]
#[path = "lib_tests.rs"]
mod tests;
