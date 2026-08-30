#![cfg(windows)]

use crayon_domain::{Checkpoint, SessionGeneration, TabId};
use crayon_platform_windows::secure_store::DpapiSecureStore;
use crayon_workflow::checkpoint::CheckpointStore;
use std::time::{SystemTime, UNIX_EPOCH};

#[test]
fn checkpoint_roundtrips_through_dpapi_without_plaintext_on_disk() {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("clock after epoch")
        .as_nanos();
    let root =
        std::env::temp_dir().join(format!("crayon-wfl04-dpapi-{}-{nonce}", std::process::id()));
    let backend = DpapiSecureStore::new(root.clone());
    let mut store = CheckpointStore::new(backend);
    let checkpoint = Checkpoint::new(
        TabId::new("tab-plaintext-canary").expect("tab id"),
        SessionGeneration::from_raw(9),
        17,
        Vec::new(),
        1_000,
        61_000,
    )
    .expect("checkpoint");

    store
        .save("task-dpapi", &checkpoint, 2_000)
        .expect("DPAPI save");
    let cipher = std::fs::read(root.join("wflcp-task-dpapi.bin")).expect("ciphertext");
    assert!(!cipher
        .windows(b"tab-plaintext-canary".len())
        .any(|window| window == b"tab-plaintext-canary"));
    assert!(
        !cipher.starts_with(b"{"),
        "DPAPI entry must not be JSON plaintext"
    );

    let taken = store.take("task-dpapi", 2_001).expect("DPAPI take");
    assert_eq!(taken.tab_id.as_str(), "tab-plaintext-canary");
    assert!(!root.join("wflcp-task-dpapi.bin").exists());
    drop(store);
    std::fs::remove_dir_all(&root).expect("remove isolated test root");
}
