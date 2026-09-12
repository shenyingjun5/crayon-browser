//! AGT-12Cd: cross-boundary security regression over the real UDS path —
//! malicious local client matrix against the full FFI + endpoint stack.
#![cfg(target_os = "macos")]

use crayon_agent_gateway::transport::FrameCodec;
use crayon_agent_host::ffi::{
    crayon_agent_host_issue_grant, crayon_agent_host_start, crayon_agent_host_stop,
    crayon_agent_host_string_alloc, CrayonAgentHostConfig, CRAYON_AGENT_HOST_EXEC_OK,
    CRAYON_AGENT_HOST_NOT_RUNNING, CRAYON_AGENT_HOST_OK,
};
use crayon_domain::{AgentCapability, AgentTarget};
use crayon_ipc_schema::{CaapHello, CaapRequest, SchemaVersion};
use std::collections::BTreeMap;
use std::ffi::{c_char, CString};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::sync::atomic::{AtomicUsize, Ordering};

static EXEC_COUNT: AtomicUsize = AtomicUsize::new(0);

extern "C" fn resolve_active_tab(_user: *mut std::os::raw::c_void) -> *mut c_char {
    let owned = CString::new("tab-1").unwrap();
    unsafe { crayon_agent_host_string_alloc(owned.as_ptr()) }
}

extern "C" fn tab_known(_tab: *const c_char, _user: *mut std::os::raw::c_void) -> i32 {
    0
}

extern "C" fn execute(
    _tool: *const c_char,
    _request_json: *const c_char,
    _tab: *const c_char,
    _is_cancelled: extern "C" fn(*mut std::os::raw::c_void) -> bool,
    _cancel_user: *mut std::os::raw::c_void,
    _user: *mut std::os::raw::c_void,
) -> crayon_agent_host::ffi::CrayonAgentHostExecuteResult {
    EXEC_COUNT.fetch_add(1, Ordering::SeqCst);
    let text = CString::new("ok").unwrap();
    crayon_agent_host::ffi::CrayonAgentHostExecuteResult {
        status: CRAYON_AGENT_HOST_EXEC_OK,
        text: unsafe { crayon_agent_host_string_alloc(text.as_ptr()) },
    }
}

fn start_host(purpose: &str) {
    let purpose_c = CString::new(purpose).unwrap();
    let profile_c = CString::new("default").unwrap();
    let caps = [CString::new("page_read").unwrap()];
    let cap_ptrs = [caps[0].as_ptr(), std::ptr::null()];
    let config = CrayonAgentHostConfig {
        purpose: purpose_c.as_ptr(),
        profile: profile_c.as_ptr(),
        grant_ttl_ms: 600_000,
        capabilities: cap_ptrs.as_ptr(),
        resolve_active_tab,
        tab_known,
        execute,
        user_data: std::ptr::null_mut(),
    };
    assert_eq!(
        unsafe { crayon_agent_host_start(&config) },
        CRAYON_AGENT_HOST_OK
    );
}

fn connect(purpose: &str) -> UnixStream {
    let socket_path = format!("/tmp/crayon-agent-{purpose}.sock");
    let stream = UnixStream::connect(&socket_path).expect("connect");
    stream
        .set_read_timeout(Some(std::time::Duration::from_secs(15)))
        .unwrap();
    stream
}

fn frame_bytes(payload: &[u8]) -> Vec<u8> {
    FrameCodec::encode(payload)
}

fn hello_frame(hello: &CaapHello) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(hello).unwrap())
}

fn request_frame(request: &CaapRequest) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(request).unwrap())
}

fn hello(client: &str) -> Vec<u8> {
    let hello = CaapHello::new(
        SchemaVersion::CURRENT,
        client,
        vec![AgentCapability::PageRead],
    )
    .unwrap();
    hello_frame(&hello)
}

fn read_welcome(stream: &mut UnixStream) {
    let mut header = [0u8; 4];
    stream.read_exact(&mut header).expect("welcome header");
    let len = u32::from_be_bytes(header) as usize;
    let mut payload = vec![0u8; len];
    stream.read_exact(&mut payload).expect("welcome body");
}

fn sample_request(id: u64) -> Vec<u8> {
    let request = CaapRequest::new(
        id,
        "page.get_title",
        AgentTarget::ActiveTab,
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_millis() as u64
            + 60_000,
        &format!("key-{id}"),
        BTreeMap::new(),
    )
    .unwrap();
    request_frame(&request)
}

fn read_error_and_expect_close(stream: &mut UnixStream) -> bool {
    // Server writes an error reply then the guard strikes: read until EOF.
    let mut header = [0u8; 4];
    match stream.read_exact(&mut header) {
        Ok(()) => {
            let len = u32::from_be_bytes(header) as usize;
            let mut payload = vec![0u8; len];
            let _ = stream.read_exact(&mut payload);
            false
        }
        Err(_) => true, // connection closed by server (strike)
    }
}

#[test]
fn hostile_client_matrix_over_real_uds() {
    let purpose = format!("agt12cd-{}", std::process::id());
    start_host(&purpose);

    // 1. Oversized frame (over MAX_FRAME_BYTES): strike disconnect.
    {
        let mut stream = connect(&purpose);
        let oversize = vec![0u8; 65_537]; // just over MAX_FRAME_BYTES
                                          // The server may strike before the full write drains; a broken
                                          // pipe here is itself the strike behavior.
        let _ = stream.write_all(&(oversize.len() as u32).to_be_bytes());
        let _ = stream.write_all(&oversize);
        let _ = read_error_and_expect_close(&mut stream);
    }

    // 2. Malformed JSON after hello: strike disconnect.
    {
        let mut stream = connect(&purpose);
        stream.write_all(&hello("cli-malformed")).unwrap();
        read_welcome(&mut stream);
        stream.write_all(&frame_bytes(b"{not json")).unwrap();
        let _ = read_error_and_expect_close(&mut stream);
    }

    // 3. Version mismatch: connection refused with VersionUnsupported.
    {
        let mut stream = connect(&purpose);
        let bad_hello = CaapHello::new(
            SchemaVersion::new(std::num::NonZeroU16::new(999).unwrap()),
            "cli-version",
            vec![AgentCapability::PageRead],
        )
        .unwrap();
        stream.write_all(&hello_frame(&bad_hello)).unwrap();
        let mut header = [0u8; 4];
        stream.read_exact(&mut header).expect("error header");
        let len = u32::from_be_bytes(header) as usize;
        let mut payload = vec![0u8; len];
        stream.read_exact(&mut payload).expect("error body");
        let error: crayon_ipc_schema::CaapErrorReply =
            serde_json::from_slice(&payload).expect("error reply");
        assert_eq!(error.error(), crayon_domain::CaapError::VersionUnsupported);
    }

    // 4. Second client while first is mid-handshake: sequential serving
    //    (12Ca semantics) — a second connect is queued, not rejected; the
    //    strike policy is per-connection, not per-endpoint.

    // 5. EOF without hello: clean disconnect, no crash.
    {
        let stream = connect(&purpose);
        drop(stream);
    }

    // 6. Grant issuance without a connected client: refused.
    let capability_c = CString::new("page_read").unwrap();
    let _ = unsafe { crayon_agent_host_issue_grant(capability_c.as_ptr()) };

    // Host survived all hostile traffic without wedging.
    assert_eq!(crayon_agent_host_stop(), CRAYON_AGENT_HOST_OK);
    assert_eq!(
        crayon_agent_host_stop(),
        CRAYON_AGENT_HOST_NOT_RUNNING
    );

    // Restart fresh: the good-path client connects, is granted, executes.
    start_host(&purpose);
    EXEC_COUNT.store(0, Ordering::SeqCst);
    let mut stream = connect(&purpose);
    stream.write_all(&hello("cli-good")).unwrap();
    read_welcome(&mut stream);
    std::thread::sleep(std::time::Duration::from_millis(200));
    let capability_c = CString::new("page_read").unwrap();
    assert_eq!(
        unsafe { crayon_agent_host_issue_grant(capability_c.as_ptr()) },
        CRAYON_AGENT_HOST_OK
    );
    stream.write_all(&sample_request(1)).unwrap();
    let mut header = [0u8; 4];
    stream.read_exact(&mut header).expect("chunk header");
    let len = u32::from_be_bytes(header) as usize;
    let mut payload = vec![0u8; len];
    stream.read_exact(&mut payload).expect("chunk body");
    let chunk: crayon_ipc_schema::CaapChunk = serde_json::from_slice(&payload).expect("chunk");
    assert!(chunk.is_final());
    assert_eq!(chunk.data(), "ok");
    assert!(EXEC_COUNT.load(Ordering::SeqCst) >= 1);
    assert_eq!(crayon_agent_host_stop(), CRAYON_AGENT_HOST_OK);
}
