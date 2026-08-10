//! Protection merge contract (MED-07): conservative precedence, BR-011 EME
//! upgrade, BR-012 blob/MediaStream, codec evidence passthrough.

use crayon_media_probe::inspect::{DashInspection, Mp4Inspection};
use crayon_media_probe::{
    assess_protection, CodecInfo, HlsEncryption, HlsPlaylist, Inspection, Protection,
    ProtectionEvidence, RenditionInfo, VariantInfo,
};

fn media(encryption: HlsEncryption) -> ProtectionEvidence {
    ProtectionEvidence::Inspection(Inspection::Hls(HlsPlaylist::Media {
        segment_uris: vec!["https://cdn.example.com/seg0.ts".to_string()],
        has_endlist: true,
        init_map_uri: None,
        encryption,
    }))
}

fn master(session_keys: Vec<HlsEncryption>) -> ProtectionEvidence {
    ProtectionEvidence::Inspection(Inspection::Hls(HlsPlaylist::Master {
        variants: vec![VariantInfo {
            uri: "https://cdn.example.com/v.m3u8".to_string(),
            bandwidth: Some(1_000_000),
            resolution: None,
            codecs: vec![],
        }],
        renditions: Vec::<RenditionInfo>::new(),
        session_keys,
    }))
}

fn verdict(evidence: &[ProtectionEvidence]) -> Protection {
    assess_protection(evidence, None).protection
}

#[test]
fn clean_streams_assess_clear() {
    assert_eq!(verdict(&[media(HlsEncryption::None)]), Protection::Clear);
    assert_eq!(
        verdict(&[ProtectionEvidence::Inspection(Inspection::Mp4(
            Mp4Inspection {
                major_brand: "mp42".to_string()
            }
        ))]),
        Protection::Clear
    );
    assert_eq!(
        verdict(&[ProtectionEvidence::Inspection(Inspection::Dash(
            DashInspection {
                has_content_protection: false,
                representation_count: 1,
            }
        ))]),
        Protection::Clear
    );
}

#[test]
fn pl_005_key_bearing_hls_is_key_required_not_clear() {
    // 当前合规姿态：凡需要 key 的加密 HLS 都不是 Clear（policy 拒绝直投）。
    assert_eq!(
        verdict(&[media(HlsEncryption::Aes128 {
            key_uri: Some("https://cdn.example.com/key.bin".to_string())
        })]),
        Protection::KeyRequired
    );
    assert_eq!(
        verdict(&[media(HlsEncryption::SampleAes { key_uri: None })]),
        Protection::KeyRequired
    );
    assert_eq!(
        verdict(&[master(vec![HlsEncryption::SessionKey { keyformat: None }])]),
        Protection::KeyRequired
    );
}

#[test]
fn pl_006_drm_facts_are_drm_protected() {
    assert_eq!(
        verdict(&[media(HlsEncryption::DrmKeyFormat {
            keyformat: "com.apple.streamingkeydelivery".to_string()
        })]),
        Protection::DrmProtected
    );
    assert_eq!(
        verdict(&[ProtectionEvidence::Inspection(Inspection::Dash(
            DashInspection {
                has_content_protection: true,
                representation_count: 2,
            }
        ))]),
        Protection::DrmProtected
    );
}

#[test]
fn br_011_eme_signal_upgrades_clear_looking_url() {
    assert_eq!(
        verdict(&[
            media(HlsEncryption::None),
            ProtectionEvidence::EmeEncryptedSignal,
        ]),
        Protection::DrmProtected,
        "EME encrypted 信号必须升级保护结论，直投被拒绝"
    );
}

#[test]
fn br_012_blob_or_stream_never_fabricates_direct_url() {
    assert_eq!(
        verdict(&[ProtectionEvidence::BlobOrStreamSource]),
        Protection::NoDirectUrl
    );
    // blob + key-required 候选：仍无直投 URL（不伪造）。
    assert_eq!(
        verdict(&[
            media(HlsEncryption::Aes128 { key_uri: None }),
            ProtectionEvidence::BlobOrStreamSource,
        ]),
        Protection::NoDirectUrl
    );
    // EME + blob：最强结论胜出。
    assert_eq!(
        verdict(&[
            ProtectionEvidence::BlobOrStreamSource,
            ProtectionEvidence::EmeEncryptedSignal,
        ]),
        Protection::DrmProtected
    );
}

#[test]
fn inconclusive_inspection_is_unknown_not_clear() {
    assert_eq!(
        verdict(&[ProtectionEvidence::Inspection(Inspection::Unknown)]),
        Protection::Unknown
    );
    // 干净证据 + 不确定证据合并：保守取 Unknown（不静默放行）。
    assert_eq!(
        verdict(&[
            media(HlsEncryption::None),
            ProtectionEvidence::Inspection(Inspection::Unknown),
        ]),
        Protection::Unknown
    );
}

#[test]
fn codec_evidence_passes_through() {
    let codecs = CodecInfo {
        video: Some("H.264".to_string()),
        audio: Some("AAC".to_string()),
        container: Some("TS".to_string()),
    };
    let assessment = assess_protection(&[media(HlsEncryption::None)], Some(codecs.clone()));
    assert_eq!(assessment.protection, Protection::Clear);
    assert_eq!(assessment.codecs, Some(codecs));
    // 未得出编码证据时保持 None，不猜测。
    assert_eq!(assess_protection(&[], None).codecs, None);
}
