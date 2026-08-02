//! get-video：视频地址解析 + 本地 relay 流服务模块。
//!
//! - `extract`：L1 静态解析 / L3 站点规则包（L2 webview 嗅探待 Tauri 壳内实现）；
//! - `relay`：axum 本地代理服务（m3u8 重写、Range 透传、防盗链伪造、SSRF 防护）；
//! - `drm`：DRM 特征检测（只检测标记，不解密）。

pub mod codec;
pub mod drm;
pub mod extract;
pub mod probe;
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
mod tests {
    use super::*;

    #[test]
    fn encode_component() {
        assert_eq!(
            encode_url_component("https://a.com/x y.m3u8?token=a&b=1"),
            "https%3A%2F%2Fa.com%2Fx%20y.m3u8%3Ftoken%3Da%26b%3D1"
        );
    }
}
