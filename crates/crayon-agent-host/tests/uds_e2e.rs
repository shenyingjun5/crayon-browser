//! AGT-12Cc1 E2E over a real same-user UDS: start the host through the C
//! ABI, connect a UnixStream client, complete Hello/Welcome → grant →
//! request → execute callback → final chunk → stop.
#![cfg(target_os = "macos")]

use crayon_agent_gateway::transport::FrameCodec;
use crayon_agent_host::ffi::{
    crayon_agent_host_issue_grant, crayon_agent_host_start, crayon_agent_host_stop,
    crayon_agent_host_string_alloc, CrayonAgentHostConfig, CRAYON_AGENT_HOST_EXEC_OK,
    CRAYON_AGENT_HOST_NOT_RUNNING, CRAYON_AGENT_HOST_OK,
};
use crayon_domain::{AgentCapability, AgentTarget};
use crayon_ipc_schema::{CaapChunk, CaapHello, CaapRequest, CaapWelcome, SchemaVersion};
use std::collections::BTreeMap;
use std::ffi::{c_char, CStr, CString};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::sync::Mutex;

static EXECUTED: Mutex<Vec<String>> = Mutex::new(Vec::new());

extern "C" fn resolve_active_tab(_user: *mut std::os::raw::c_void) -> *mut c_char {
    let owned = CString::new("tab-1").expect("tab");
    unsafe { crayon_agent_host_string_alloc(owned.as_ptr()) }
}

extern "C" fn tab_known(_tab: *const c_char, _user: *mut std::os::raw::c_void) -> i32 {
    0
}

extern "C" fn execute(
    tool: *const c_char,
    _request_json: *const c_char,
    _tab: *const c_char,
    _is_cancelled: extern "C" fn(*mut std::os::raw::c_void) -> bool,
    _cancel_user: *mut std::os::raw::c_void,
    _user: *mut std::os::raw::c_void,
) -> crayon_agent_host::ffi::CrayonAgentHostExecuteResult {
    let tool_name = unsafe { CStr::from_ptr(tool) }
        .to_string_lossy()
        .into_owned();
    EXECUTED.lock().unwrap().push(tool_name);
    let text = CString::new("The Title").expect("text");
    crayon_agent_host::ffi::CrayonAgentHostExecuteResult {
        status: CRAYON_AGENT_HOST_EXEC_OK,
        text: unsafe { crayon_agent_host_string_alloc(text.as_ptr()) },
    }
}

fn frame_bytes<T: serde::ser::Serialize>(value: &T) -> Vec<u8> {
    FrameCodec::encode(&serde_json::to_vec(value).unwrap())
}

#[test]
fn full_roundtrip_over_real_uds() {
    let purpose = format!("agent-host-e2e-{}", std::process::id());
    let purpose_c = CString::new(purpose.clone()).unwrap();
    let profile_c = CString::new("default").unwrap();
    let page_read = CString::new("page_read").unwrap();
    let caps = [page_read.as_ptr(), std::ptr::null()];

    let config = CrayonAgentHostConfig {
        purpose: purpose_c.as_ptr(),
        profile: profile_c.as_ptr(),
        grant_ttl_ms: 600_000,
        capabilities: caps.as_ptr(),
        resolve_active_tab,
        tab_known,
        execute,
        user_data: std::ptr::null_mut(),
    };
    eprintln!("[T] started");
    assert_eq!(
        unsafe { crayon_agent_host_start(&config) },
        CRAYON_AGENT_HOST_OK
    );
    // Double start is rejected.
    assert_ne!(
        unsafe { crayon_agent_host_start(&config) },
        CRAYON_AGENT_HOST_OK
    );

    // Client: same-user UnixStream to the derived socket path.
    let socket_path = format!("/tmp/crayon-agent-{purpose}.sock");
    let mut stream = UnixStream::connect(&socket_path).expect("connect");
    eprintln!("[T] connected");
    stream
        .set_read_timeout(Some(std::time::Duration::from_secs(10)))
        .unwrap();

    // Hello.
    let hello = CaapHello::new(
        SchemaVersion::CURRENT,
        "e2e-client",
        vec![AgentCapability::PageRead],
    )
    .unwrap();
    stream.write_all(&frame_bytes(&hello)).unwrap();
    let mut codec = FrameCodec::new();
    let welcome_payload = loop {
        match codec.feed(&stream_read_chunk(&mut stream)) {
            Ok(crayon_agent_gateway::transport::DecodedFrame::Complete(payload)) => break payload,
            Ok(_) => continue,
            Err(error) => panic!("welcome failed: {error:?}"),
        }
    };
    let welcome: CaapWelcome = serde_json::from_slice(&welcome_payload).unwrap();
    assert_eq!(welcome.schema(), SchemaVersion::CURRENT);
    eprintln!("[T] welcome ok");
    assert!(welcome.capabilities().contains(&AgentCapability::PageRead));

    // Grant (AGT-05 confirmation outcome).
    let capability_c = CString::new("page_read").unwrap();
    eprintln!("[T] issuing grant");
    assert_eq!(
        unsafe { crayon_agent_host_issue_grant(capability_c.as_ptr()) },
        CRAYON_AGENT_HOST_OK
    );
    eprintln!("[T] granted");

    // Request.
    let deadline = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64
        + 600_000;
    let request = CaapRequest::new(
        41,
        "page.get_title",
        AgentTarget::ActiveTab,
        deadline,
        "e2e-key",
        BTreeMap::new(),
    )
    .unwrap();
    stream.write_all(&frame_bytes(&request)).unwrap();

    eprintln!("[T] request sent, reading chunks");
    let mut chunks = Vec::new();
    loop {
        let payload = loop {
            match codec.feed(&stream_read_chunk(&mut stream)) {
                Ok(crayon_agent_gateway::transport::DecodedFrame::Complete(payload)) => {
                    break payload
                }
                Ok(_) => continue,
                Err(error) => panic!("chunk failed: {error:?}"),
            }
        };
        let chunk: CaapChunk = serde_json::from_slice(&payload).unwrap();
        let is_final = chunk.is_final();
        chunks.push(chunk);
        if is_final {
            break;
        }
    }
    assert_eq!(chunks.len(), 1);
    assert_eq!(chunks[0].id(), 41);
    assert_eq!(chunks[0].data(), "The Title");
    assert_eq!(EXECUTED.lock().unwrap().as_slice(), ["page.get_title"]);

    // Client disconnects; the serve thread then observes the stop flag.
    drop(stream);
    assert_eq!(crayon_agent_host_stop(), CRAYON_AGENT_HOST_OK);
    assert_eq!(crayon_agent_host_stop(), CRAYON_AGENT_HOST_NOT_RUNNING);
}

fn stream_read_chunk(stream: &mut UnixStream) -> Vec<u8> {
    let mut buffer = [0u8; 4096];
    let read = stream.read(&mut buffer).expect("stream read");
    buffer[..read].to_vec()
}
