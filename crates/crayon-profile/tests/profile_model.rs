//! Behaviour tests for the profile model.  All cases are deterministic;
//! randomness-sensitive paths inject fixed directory IDs.

use crayon_profile::{
    DirectoryId, ProfileError, ProfileId, ProfileIdError, ProfileLifecycle, ProfileRegistry,
    ProfileType, MAX_PROFILES,
};
use std::path::PathBuf;

fn registry() -> ProfileRegistry {
    ProfileRegistry::new(PathBuf::from("/profiles-root")).expect("absolute root")
}

fn fixed_directory(byte: u8) -> DirectoryId {
    DirectoryId::from_bytes([byte; 16])
}

// ---------- ProfileId ----------

#[test]
fn profile_id_validation_matrix() {
    assert_eq!(ProfileId::new(""), Err(ProfileIdError::Empty));
    assert_eq!(
        ProfileId::new(&"x".repeat(257)),
        Err(ProfileIdError::TooLong)
    );
    assert!(ProfileId::new(&"x".repeat(256)).is_ok());
    assert!(ProfileId::new("默认").is_ok()); // UTF-8 accepted
    assert_eq!(ProfileId::new("work").expect("valid").as_str(), "work");
}

// ---------- DirectoryId ----------

#[test]
fn directory_id_hex_format_is_stable() {
    let id = DirectoryId::from_bytes([0xAB; 16]);
    let hex = id.to_hex();
    assert_eq!(hex.len(), 32);
    assert_eq!(hex, "abababababababababababababababab");
    assert_eq!(format!("{id}"), hex);
}

#[test]
fn generated_directory_ids_are_unique() {
    let first = DirectoryId::generate().expect("entropy available");
    let second = DirectoryId::generate().expect("entropy available");
    assert_ne!(first, second);
}

// ---------- Registry creation ----------

#[test]
fn registry_requires_absolute_root() {
    assert!(matches!(
        ProfileRegistry::new(PathBuf::from("relative/root")),
        Err(ProfileError::IllegalState)
    ));
}

#[test]
fn create_and_find_profile() {
    let mut registry = registry();
    registry
        .create_profile_with_directory("work", ProfileType::Regular, fixed_directory(1))
        .expect("create");
    let id = ProfileId::new("work").expect("id");
    let profile = registry.find(&id).expect("present");
    assert!(profile.is_active());
    assert_eq!(profile.lifecycle(), ProfileLifecycle::Active);
    assert_eq!(profile.profile_type(), ProfileType::Regular);
    assert!(!profile.profile_type().is_ephemeral());
    assert_eq!(registry.profile_count(), 1);
}

#[test]
fn incognito_profile_is_ephemeral() {
    let mut registry = registry();
    registry
        .create_profile_with_directory("private", ProfileType::Incognito, fixed_directory(2))
        .expect("create");
    let id = ProfileId::new("private").expect("id");
    let profile = registry.find(&id).expect("present");
    assert!(profile.profile_type().is_ephemeral());
}

#[test]
fn duplicate_and_unknown_ids_rejected() {
    let mut registry = registry();
    registry
        .create_profile_with_directory("work", ProfileType::Regular, fixed_directory(1))
        .expect("create");
    let id = ProfileId::new("work").expect("id");
    assert_eq!(
        registry.create_profile_with_directory("work", ProfileType::Regular, fixed_directory(9)),
        Err(ProfileError::DuplicateId)
    );
    // Directory IDs are single-owner: sharing breaks isolation.
    assert_eq!(
        registry.create_profile_with_directory("other", ProfileType::Regular, fixed_directory(1)),
        Err(ProfileError::DirectoryIdInUse)
    );
    assert_eq!(
        registry.begin_close(&ProfileId::new("missing").expect("id")),
        Err(ProfileError::UnknownId)
    );
    assert_eq!(
        registry.finish_close(&id),
        Err(ProfileError::IllegalState) // cannot finish before begin
    );
    assert_eq!(
        registry.remove(&id),
        Err(ProfileError::IllegalState) // still active
    );
}

#[test]
fn invalid_profile_id_rejected_at_registry() {
    let mut registry = registry();
    assert_eq!(
        registry.create_profile_with_directory("", ProfileType::Regular, fixed_directory(1)),
        Err(ProfileError::InvalidId(ProfileIdError::Empty))
    );
}

#[test]
fn registry_capacity_enforced() {
    let mut registry = registry();
    for index in 0..MAX_PROFILES {
        registry
            .create_profile_with_directory(
                &format!("profile-{index}"),
                ProfileType::Regular,
                fixed_directory((index % 256) as u8),
            )
            .expect("within capacity");
    }
    assert_eq!(
        registry.create_profile_with_directory(
            "overflow",
            ProfileType::Regular,
            fixed_directory(255),
        ),
        Err(ProfileError::Capacity)
    );
}

// ---------- Path derivation ----------

#[test]
fn path_uses_directory_id_never_profile_id() {
    let mut registry = registry();
    registry
        .create_profile_with_directory(
            "display name 名称",
            ProfileType::Regular,
            fixed_directory(7),
        )
        .expect("create");
    let id = ProfileId::new("display name 名称").expect("id");
    let path = registry.profile_path(&id).expect("present");
    let component = path
        .file_name()
        .expect("file name")
        .to_str()
        .expect("hex is ascii");
    assert_eq!(component, "07070707070707070707070707070707");
    assert!(!path.to_string_lossy().contains("display name"));
    assert!(path.starts_with("/profiles-root"));
}

#[test]
fn unknown_profile_has_no_path() {
    let registry = registry();
    let id = ProfileId::new("missing").expect("id");
    assert!(registry.profile_path(&id).is_none());
    assert!(registry.find(&id).is_none());
}

// ---------- Lifecycle ----------

#[test]
fn close_lifecycle_and_removal() {
    let mut registry = registry();
    registry
        .create_profile_with_directory("work", ProfileType::Regular, fixed_directory(1))
        .expect("create");
    let id = ProfileId::new("work").expect("id");

    registry.begin_close(&id).expect("begin close");
    assert_eq!(
        registry.find(&id).expect("present").lifecycle(),
        ProfileLifecycle::Closing
    );
    assert!(!registry.find(&id).expect("present").is_active());

    // Repeated closes are stable rejections, not side effects.
    assert_eq!(registry.begin_close(&id), Err(ProfileError::IllegalState));
    assert_eq!(
        registry.remove(&id),
        Err(ProfileError::IllegalState) // closing, not yet closed
    );

    registry.finish_close(&id).expect("finish close");
    assert_eq!(
        registry.find(&id).expect("present").lifecycle(),
        ProfileLifecycle::Closed
    );
    assert_eq!(registry.begin_close(&id), Err(ProfileError::IllegalState));
    assert_eq!(registry.finish_close(&id), Err(ProfileError::IllegalState));

    registry.remove(&id).expect("removable once closed");
    assert!(registry.find(&id).is_none());
    assert_eq!(registry.profile_count(), 0);
}

#[test]
fn remove_unknown_profile_rejected() {
    let mut registry = registry();
    assert_eq!(
        registry.remove(&ProfileId::new("missing").expect("id")),
        Err(ProfileError::UnknownId)
    );
}
