//! Platform and receiver capability models (technical design §4.1, §9.1).
//!
//! Capabilities are collected once at startup by the platform adapter and are
//! read-only afterwards. Shared policy code must branch on these declared
//! capabilities, never on OS or device-model checks. The wire form must not
//! carry user identity or page URLs.

use serde::{Deserialize, Serialize};

/// Browser engine in use.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum BrowserEngineKind {
    Cef,
    ArkWeb,
}

/// Local device-discovery transport offered by the platform.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum LocalDiscoveryKind {
    #[serde(rename = "mdns+udp")]
    MdnsUdp,
    #[serde(rename = "unavailable")]
    Unavailable,
}

/// OS-backed secure storage availability.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub enum SecureStoreKind {
    #[serde(rename = "os_native")]
    OsNative,
    #[serde(rename = "unavailable")]
    Unavailable,
}

/// Whether the platform blocks capturing DRM-protected surfaces.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProtectedSurfaceKind {
    /// Protected video is blacked out by the OS; mirror capture degrades.
    Blocked,
    /// No protected-surface restriction applies.
    Capturable,
}

/// Read-only platform capability set generated once at startup (§4.1).
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PlatformCapabilities {
    browser_engine: BrowserEngineKind,
    tab_video: bool,
    system_audio: bool,
    hardware_h264: bool,
    local_discovery: LocalDiscoveryKind,
    secure_store: SecureStoreKind,
    protected_surface: ProtectedSurfaceKind,
}

impl PlatformCapabilities {
    #[must_use]
    pub const fn new(
        browser_engine: BrowserEngineKind,
        tab_video: bool,
        system_audio: bool,
        hardware_h264: bool,
        local_discovery: LocalDiscoveryKind,
        secure_store: SecureStoreKind,
        protected_surface: ProtectedSurfaceKind,
    ) -> Self {
        Self {
            browser_engine,
            tab_video,
            system_audio,
            hardware_h264,
            local_discovery,
            secure_store,
            protected_surface,
        }
    }

    #[must_use]
    pub const fn browser_engine(self) -> BrowserEngineKind {
        self.browser_engine
    }
    #[must_use]
    pub const fn tab_video(self) -> bool {
        self.tab_video
    }
    #[must_use]
    pub const fn system_audio(self) -> bool {
        self.system_audio
    }
    #[must_use]
    pub const fn hardware_h264(self) -> bool {
        self.hardware_h264
    }
    #[must_use]
    pub const fn local_discovery(self) -> LocalDiscoveryKind {
        self.local_discovery
    }
    #[must_use]
    pub const fn secure_store(self) -> SecureStoreKind {
        self.secure_store
    }
    #[must_use]
    pub const fn protected_surface(self) -> ProtectedSurfaceKind {
        self.protected_surface
    }
}

/// Receiver capabilities, always sourced from Cast-SDK assessment (§6 of the
/// architecture contract) — never guessed by UI or site rules.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReceiverCapabilities {
    mp4: bool,
    hls: bool,
    dash: bool,
    h264: bool,
    hevc: bool,
    av1: bool,
    /// Maximum supported video height in pixels (e.g. 2160 for 4K).
    max_height: u16,
}

impl ReceiverCapabilities {
    #[must_use]
    #[allow(clippy::too_many_arguments)]
    pub const fn new(
        mp4: bool,
        hls: bool,
        dash: bool,
        h264: bool,
        hevc: bool,
        av1: bool,
        max_height: u16,
    ) -> Self {
        Self {
            mp4,
            hls,
            dash,
            h264,
            hevc,
            av1,
            max_height,
        }
    }

    #[must_use]
    pub const fn mp4(self) -> bool {
        self.mp4
    }
    #[must_use]
    pub const fn hls(self) -> bool {
        self.hls
    }
    #[must_use]
    pub const fn dash(self) -> bool {
        self.dash
    }
    #[must_use]
    pub const fn h264(self) -> bool {
        self.h264
    }
    #[must_use]
    pub const fn hevc(self) -> bool {
        self.hevc
    }
    #[must_use]
    pub const fn av1(self) -> bool {
        self.av1
    }
    #[must_use]
    pub const fn max_height(self) -> u16 {
        self.max_height
    }
}
