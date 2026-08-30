use crate::checkpoint::{CheckpointStore, CheckpointStoreError};
use crayon_domain::{Checkpoint, CheckpointState, SessionGeneration, TabId};
use crayon_platform_api::secure_store::{SecureStore, SecureStoreError};
use std::collections::BTreeMap;

#[derive(Default)]
struct MemorySecureStore {
    entries: BTreeMap<String, Vec<u8>>,
    load_error: Option<SecureStoreError>,
    delete_error: Option<SecureStoreError>,
}

impl SecureStore for MemorySecureStore {
    fn store(&mut self, key: &str, value: &[u8]) -> Result<(), SecureStoreError> {
        self.entries.insert(key.to_owned(), value.to_vec());
        Ok(())
    }

    fn load(&self, key: &str) -> Result<Option<Vec<u8>>, SecureStoreError> {
        if let Some(error) = self.load_error {
            return Err(error);
        }
        Ok(self.entries.get(key).cloned())
    }

    fn delete(&mut self, key: &str) -> Result<(), SecureStoreError> {
        if let Some(error) = self.delete_error.take() {
            return Err(error);
        }
        self.entries.remove(key);
        Ok(())
    }
}

fn checkpoint(payload: Vec<u8>) -> Checkpoint {
    Checkpoint::new(
        TabId::new("tab-1").expect("tab id"),
        SessionGeneration::from_raw(7),
        11,
        payload,
        1_000,
        61_000,
    )
    .expect("checkpoint")
}

#[test]
fn saves_and_consumes_minimal_checkpoint_once() {
    let mut store = CheckpointStore::new(MemorySecureStore::default());
    store
        .save("task-1", &checkpoint(Vec::new()), 2_000)
        .expect("save");
    let taken = store.take("task-1", 2_001).expect("take");
    assert_eq!(taken.state, CheckpointState::Consumed);
    assert!(taken.payload.is_empty());
    assert_eq!(
        store.take("task-1", 2_002),
        Err(CheckpointStoreError::NotFound)
    );
}

#[test]
fn rejects_payload_and_invalid_ids_before_backend_write() {
    let mut store = CheckpointStore::new(MemorySecureStore::default());
    assert_eq!(
        store.save(
            "task-1",
            &checkpoint(b"page body or secret".to_vec()),
            2_000
        ),
        Err(CheckpointStoreError::InvalidCheckpoint)
    );
    assert_eq!(
        store.save("../escape", &checkpoint(Vec::new()), 2_000),
        Err(CheckpointStoreError::InvalidId)
    );
    assert!(store.into_inner().entries.is_empty());
}

#[test]
fn expired_and_corrupt_records_are_cleared() {
    let mut store = CheckpointStore::new(MemorySecureStore::default());
    store
        .save("expired", &checkpoint(Vec::new()), 2_000)
        .expect("save");
    assert_eq!(
        store.take("expired", 61_000),
        Err(CheckpointStoreError::Expired)
    );

    let mut backend = store.into_inner();
    backend
        .entries
        .insert("wflcp-corrupt".to_owned(), b"not-json".to_vec());
    let mut store = CheckpointStore::new(backend);
    assert_eq!(
        store.take("corrupt", 2_000),
        Err(CheckpointStoreError::InvalidCheckpoint)
    );
    assert!(store.into_inner().entries.is_empty());
}

#[test]
fn unknown_fields_and_non_live_records_are_rejected_and_cleared() {
    let mut wire = serde_json::to_value(checkpoint(Vec::new())).expect("value");
    wire.as_object_mut()
        .expect("object")
        .insert("extra".to_owned(), true.into());
    let mut backend = MemorySecureStore::default();
    backend.entries.insert(
        "wflcp-unknown".to_owned(),
        serde_json::to_vec(&wire).expect("wire"),
    );
    let mut consumed = checkpoint(Vec::new());
    consumed.consume(2_000).expect("consume");
    backend.entries.insert(
        "wflcp-consumed".to_owned(),
        serde_json::to_vec(&consumed).expect("wire"),
    );
    let mut store = CheckpointStore::new(backend);
    assert_eq!(
        store.take("unknown", 2_000),
        Err(CheckpointStoreError::InvalidCheckpoint)
    );
    assert_eq!(
        store.take("consumed", 2_000),
        Err(CheckpointStoreError::InvalidCheckpoint)
    );
}

#[test]
fn clear_is_idempotent_and_backend_failures_are_closed() {
    let mut store = CheckpointStore::new(MemorySecureStore::default());
    store.clear("missing").expect("idempotent clear");
    let mut backend = store.into_inner();
    backend.load_error = Some(SecureStoreError::AccessDenied);
    let mut store = CheckpointStore::new(backend);
    assert_eq!(
        store.take("task-1", 2_000),
        Err(CheckpointStoreError::Backend(
            SecureStoreError::AccessDenied
        ))
    );

    let mut backend = MemorySecureStore::default();
    backend
        .entries
        .insert("wflcp-task-1".to_owned(), b"broken".to_vec());
    backend.delete_error = Some(SecureStoreError::Unavailable);
    let mut store = CheckpointStore::new(backend);
    assert_eq!(
        store.take("task-1", 2_000),
        Err(CheckpointStoreError::Backend(SecureStoreError::Unavailable))
    );
}
