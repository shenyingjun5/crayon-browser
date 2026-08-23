//! HUB-01 capability descriptor schema tests: closed vocabularies,
//! validation matrix, serde wire forms and the deterministic wire tag.

use super::*;

#[test]
fn capability_token_charset_and_bounds() {
    assert!(is_capability_token("browser.content"));
    assert!(is_capability_token("cast-control:2"));
    assert!(is_capability_token("a"));
    let exact = "a".repeat(MAX_CAPABILITY_ID_LEN);
    assert!(is_capability_token(&exact));
    let overlong = "a".repeat(MAX_CAPABILITY_ID_LEN + 1);
    for bad in [
        "",
        "Browser.Content",
        "browser content",
        "页面",
        "a\nb",
        overlong.as_str(),
    ] {
        assert!(!is_capability_token(bad), "token {bad:?} must be rejected");
    }
}

fn descriptor(id: &str, source: CapabilitySource, trust: TrustLevel) -> CapabilityDescriptor {
    CapabilityDescriptor {
        id: id.to_owned(),
        version: "1.0.0".to_owned(),
        source,
        trust,
        data_scope: DataScope::LocalOnly,
        summary: "test capability".to_owned(),
    }
}

#[test]
fn descriptor_validation_matrix() {
    assert!(descriptor(
        "browser.content",
        CapabilitySource::Builtin,
        TrustLevel::System
    )
    .validate()
    .is_ok());
    // Invalid ids.
    for id in ["", "UPPER", "with space"] {
        assert_eq!(
            descriptor(id, CapabilitySource::Builtin, TrustLevel::Untrusted).validate(),
            Err(CapabilitySchemaError::InvalidId),
            "id {id:?} must be rejected"
        );
    }
    // Invalid versions: charset, emptiness and the 32-byte bound.
    let mut base = descriptor(
        "probe.cap",
        CapabilitySource::Builtin,
        TrustLevel::Untrusted,
    );
    base.version = String::new();
    assert_eq!(base.validate(), Err(CapabilitySchemaError::InvalidVersion));
    base.version = "1.0.0 UPPER".to_owned();
    assert_eq!(base.validate(), Err(CapabilitySchemaError::InvalidVersion));
    base.version = "a".repeat(MAX_CAPABILITY_VERSION_LEN + 1);
    assert_eq!(base.validate(), Err(CapabilitySchemaError::InvalidVersion));
    base.version = "a".repeat(MAX_CAPABILITY_VERSION_LEN);
    assert!(base.validate().is_ok());
    // Summary bound: exactly 256 bytes passes, one more fails.
    base.summary = "s".repeat(MAX_CAPABILITY_SUMMARY_LEN);
    assert!(base.validate().is_ok());
    base.summary = "s".repeat(MAX_CAPABILITY_SUMMARY_LEN + 1);
    assert_eq!(base.validate(), Err(CapabilitySchemaError::SummaryTooLong));
}

#[test]
fn partner_source_can_never_claim_system_trust() {
    assert_eq!(
        descriptor("partner.cap", CapabilitySource::Partner, TrustLevel::System).validate(),
        Err(CapabilitySchemaError::TrustConflict)
    );
    for trust in [TrustLevel::Untrusted, TrustLevel::UserApproved] {
        assert!(
            descriptor("partner.cap", CapabilitySource::Partner, trust)
                .validate()
                .is_ok(),
            "{trust:?} is legal for partner sources"
        );
    }
    for source in [CapabilitySource::Builtin, CapabilitySource::PersonalSkill] {
        assert!(
            descriptor("trusted.cap", source, TrustLevel::System)
                .validate()
                .is_ok(),
            "{source:?} may declare system trust"
        );
    }
}

#[test]
fn precedence_is_builtin_over_personal_over_partner() {
    assert!(CapabilitySource::Builtin.precedence() > CapabilitySource::PersonalSkill.precedence());
    assert!(CapabilitySource::PersonalSkill.precedence() > CapabilitySource::Partner.precedence());
}

#[test]
fn wire_names_are_closed_snake_case() {
    let sources = [
        (CapabilitySource::Partner, "partner"),
        (CapabilitySource::PersonalSkill, "personal_skill"),
        (CapabilitySource::Builtin, "builtin"),
    ];
    let trusts = [
        (TrustLevel::Untrusted, "untrusted"),
        (TrustLevel::UserApproved, "user_approved"),
        (TrustLevel::System, "system"),
    ];
    let lifecycles = [
        (LifecycleState::Active, "active"),
        (LifecycleState::Disabled, "disabled"),
        (LifecycleState::Revoked, "revoked"),
    ];
    let scopes = [
        (DataScope::LocalOnly, "local_only"),
        (DataScope::PageContent, "page_content"),
        (DataScope::CastControl, "cast_control"),
        (DataScope::ExternalEndpoint, "external_endpoint"),
    ];
    fn assert_roundtrip<T>(value: &T, wire: &str)
    where
        T: serde::Serialize + serde::de::DeserializeOwned + PartialEq + std::fmt::Debug,
    {
        let json = serde_json::to_string(value).expect("enum serializes");
        assert_eq!(
            json,
            format!("\"{wire}\""),
            "serde form must match wire name"
        );
        let back: T = serde_json::from_str(&json).expect("wire form deserializes");
        assert_eq!(&back, value, "wire form must roundtrip");
    }
    for (value, wire) in sources {
        assert_roundtrip(&value, wire);
    }
    for (value, wire) in trusts {
        assert_roundtrip(&value, wire);
    }
    for (value, wire) in lifecycles {
        assert_roundtrip(&value, wire);
    }
    for (value, wire) in scopes {
        assert_roundtrip(&value, wire);
    }
}

#[test]
fn wire_tag_format_is_stable() {
    let tag = descriptor(
        "browser.content",
        CapabilitySource::Builtin,
        TrustLevel::System,
    )
    .wire_tag();
    assert_eq!(tag, "browser.content@1.0.0:builtin:system:local_only");
    let tag = CapabilityDescriptor {
        id: "skill.export".to_owned(),
        version: "2.1-beta".to_owned(),
        source: CapabilitySource::PersonalSkill,
        trust: TrustLevel::UserApproved,
        data_scope: DataScope::PageContent,
        summary: String::new(),
    }
    .wire_tag();
    assert_eq!(
        tag,
        "skill.export@2.1-beta:personal_skill:user_approved:page_content"
    );
}
