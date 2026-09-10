//! C ABI surface for embedding the CAAP host in the desktop product
//! (AGT-12Cc1). Every entry point is panic-free (`catch_unwind`) and
//! returns a stable integer code; strings cross the boundary as UTF-8
//! pointers allocated by `crayon_agent_host_string_alloc` and released by
//! `crayon_agent_host_string_free`. Payload bytes never cross this layer
//! for logging or diagnostics.

use std::ffi::{c_char, CStr, CString};
use std::sync::Mutex;

use crayon_agent_gateway::grant::ProfileScope;
use crayon_domain::{AgentCapability, AgentTarget, CaapError, TabId};
use crayon_ipc_schema::CaapRequest;

use crate::{start_macos_uds, AgentHost, CancelFlag, ToolPort};
use crayon_ipc_schema::SchemaVersion;

/// Status codes for the host entry points.
pub const CRAYON_AGENT_HOST_OK: i32 = 0;
pub const CRAYON_AGENT_HOST_ALREADY_RUNNING: i32 = 1;
pub const CRAYON_AGENT_HOST_INVALID_CONFIG: i32 = 2;
pub const CRAYON_AGENT_HOST_ENDPOINT_FAILED: i32 = 3;
pub const CRAYON_AGENT_HOST_UNSUPPORTED_PLATFORM: i32 = 4;
pub const CRAYON_AGENT_HOST_NOT_RUNNING: i32 = 5;
pub const CRAYON_AGENT_HOST_POISONED: i32 = 6;
pub const CRAYON_AGENT_HOST_UNKNOWN_CAPABILITY: i32 = 7;

/// Execute-result status: the tool ran and produced `text`.
pub const CRAYON_AGENT_HOST_EXEC_OK: i32 = 0;
/// Execute-result status: the tool observed the cancellation flag.
pub const CRAYON_AGENT_HOST_EXEC_CANCELLED: i32 = 1;
/// Execute-result status: a stable CAAP error at `error_index` follows.
pub const CRAYON_AGENT_HOST_EXEC_CAAP_ERROR: i32 = 2;

/// Resolves the active tab id (UTF-8, NUL-terminated) or null on failure.
pub type CrayonAgentHostResolveFn = extern "C" fn(user: *mut std::os::raw::c_void) -> *mut c_char;

/// Validates a targeted tab id. Returns 0 when known.
pub type CrayonAgentHostTabKnownFn =
    extern "C" fn(tab: *const c_char, user: *mut std::os::raw::c_void) -> i32;

/// Result of one tool execution. `text` must be allocated by
/// `crayon_agent_host_string_alloc` (or null) and is released by the host.
#[repr(C)]
pub struct CrayonAgentHostExecuteResult {
    pub status: i32,
    pub text: *mut c_char,
}

/// Executes one confirmed, authorized tool.
pub type CrayonAgentHostExecuteFn = extern "C" fn(
    tool: *const c_char,
    request_json: *const c_char,
    tab: *const c_char,
    is_cancelled: extern "C" fn(user: *mut std::os::raw::c_void) -> bool,
    cancel_user: *mut std::os::raw::c_void,
    user: *mut std::os::raw::c_void,
) -> CrayonAgentHostExecuteResult;

/// Host configuration. Strings are borrowed for the duration of `start`.
#[repr(C)]
pub struct CrayonAgentHostConfig {
    pub purpose: *const c_char,
    pub profile: *const c_char,
    pub grant_ttl_ms: u64,
    /// NUL-terminated array of capability names (`page_read`, ...).
    pub capabilities: *const *const c_char,
    pub resolve_active_tab: CrayonAgentHostResolveFn,
    pub tab_known: CrayonAgentHostTabKnownFn,
    pub execute: CrayonAgentHostExecuteFn,
    pub user_data: *mut std::os::raw::c_void,
}

/// FFI tool port: wraps the config callbacks. The product guarantees the
/// callbacks and user data outlive `stop`; sends across the serve-thread
/// boundary are therefore sound.
struct FfiPort {
    resolve_active_tab: CrayonAgentHostResolveFn,
    tab_known: CrayonAgentHostTabKnownFn,
    execute: CrayonAgentHostExecuteFn,
    user: *mut std::os::raw::c_void,
}

unsafe impl Send for FfiPort {}

extern "C" fn is_cancelled_trampoline(user: *mut std::os::raw::c_void) -> bool {
    let flag = unsafe { &*(user as *const CancelFlag) };
    flag.is_cancelled()
}

impl ToolPort for FfiPort {
    fn resolve_target(&mut self, target: &AgentTarget) -> Result<TabId, CaapError> {
        match target {
            AgentTarget::ActiveTab => {
                let raw = (self.resolve_active_tab)(self.user);
                if raw.is_null() {
                    return Err(CaapError::TargetInvalid);
                }
                let text = unsafe { CStr::from_ptr(raw) };
                let tab = TabId::new(text.to_string_lossy().as_ref())
                    .map_err(|_| CaapError::TargetInvalid);
                unsafe {
                    crayon_agent_host_string_free(raw as *mut c_char);
                }
                tab
            }
            AgentTarget::Tab { tab } => {
                let name = CString::new(tab.as_str()).map_err(|_| CaapError::TargetInvalid)?;
                if (self.tab_known)(name.as_ptr(), self.user) == 0 {
                    Ok(tab.clone())
                } else {
                    Err(CaapError::TargetInvalid)
                }
            }
        }
    }

    fn execute(
        &mut self,
        tool: &str,
        request: &CaapRequest,
        tab: &TabId,
        cancel: &CancelFlag,
    ) -> Result<String, CaapError> {
        let tool_c = CString::new(tool).map_err(|_| CaapError::InvalidMessage)?;
        let request_json = serde_json::to_string(request).map_err(|_| CaapError::InvalidMessage)?;
        let request_c = CString::new(request_json).map_err(|_| CaapError::InvalidMessage)?;
        let tab_c = CString::new(tab.as_str()).map_err(|_| CaapError::InvalidMessage)?;

        let result = (self.execute)(
            tool_c.as_ptr(),
            request_c.as_ptr(),
            tab_c.as_ptr(),
            is_cancelled_trampoline,
            cancel as *const CancelFlag as *mut std::os::raw::c_void,
            self.user,
        );
        let text = if result.text.is_null() {
            String::new()
        } else {
            let owned = unsafe { CString::from_raw(result.text) };
            owned.to_string_lossy().into_owned()
        };
        match result.status {
            CRAYON_AGENT_HOST_EXEC_OK => Ok(text),
            CRAYON_AGENT_HOST_EXEC_CANCELLED => Err(CaapError::Cancelled),
            status => {
                let index = (status - CRAYON_AGENT_HOST_EXEC_CAAP_ERROR).clamp(0, 9) as usize;
                Err(caap_error_at(index))
            }
        }
    }
}

fn caap_error_at(index: usize) -> CaapError {
    const ALL: [CaapError; 10] = [
        CaapError::VersionUnsupported,
        CaapError::CapabilityDenied,
        CaapError::ToolUnknown,
        CaapError::TargetInvalid,
        CaapError::TargetStale,
        CaapError::Cancelled,
        CaapError::DeadlineExceeded,
        CaapError::QueueFull,
        CaapError::Unauthorized,
        CaapError::InvalidMessage,
    ];
    ALL.get(index).copied().unwrap_or(CaapError::InvalidMessage)
}

fn capability_from_name(name: &str) -> Option<AgentCapability> {
    match name {
        "page_read" => Some(AgentCapability::PageRead),
        "navigation" => Some(AgentCapability::Navigation),
        "cast_read" => Some(AgentCapability::CastRead),
        "cast_control" => Some(AgentCapability::CastControl),
        "semantic_action" => Some(AgentCapability::SemanticAction),
        _ => None,
    }
}

struct GlobalHost {
    agent: AgentHost<FfiPort>,
    socket_path: String,
}

static HOST: Mutex<Option<GlobalHost>> = Mutex::new(None);

/// Starts the host. Fails with ALREADY_RUNNING when a host is live.
///
/// # Safety
///
/// `config` and every borrowed string must be valid for the duration of
/// the call; the callbacks and `user_data` must outlive
/// `crayon_agent_host_stop`.
#[no_mangle]
pub unsafe extern "C" fn crayon_agent_host_start(config: *const CrayonAgentHostConfig) -> i32 {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let Some(config) = config.as_ref() else {
            return CRAYON_AGENT_HOST_INVALID_CONFIG;
        };
        if config.purpose.is_null() || config.profile.is_null() {
            return CRAYON_AGENT_HOST_INVALID_CONFIG;
        }
        let purpose = unsafe { CStr::from_ptr(config.purpose) }
            .to_string_lossy()
            .into_owned();
        let profile_name = unsafe { CStr::from_ptr(config.profile) }
            .to_string_lossy()
            .into_owned();
        let Ok(profile) = ProfileScope::new(&profile_name) else {
            return CRAYON_AGENT_HOST_INVALID_CONFIG;
        };
        let mut capabilities = Vec::new();
        if !config.capabilities.is_null() {
            let mut index = 0usize;
            loop {
                let entry = unsafe { *config.capabilities.add(index) };
                if entry.is_null() {
                    break;
                }
                let name = unsafe { CStr::from_ptr(entry) }
                    .to_string_lossy()
                    .into_owned();
                match capability_from_name(&name) {
                    Some(capability) => capabilities.push(capability),
                    None => return CRAYON_AGENT_HOST_UNKNOWN_CAPABILITY,
                }
                index += 1;
            }
        }
        let port = FfiPort {
            resolve_active_tab: config.resolve_active_tab,
            tab_known: config.tab_known,
            execute: config.execute,
            user: config.user_data,
        };
        let Ok(mut host) = HOST.lock() else {
            return CRAYON_AGENT_HOST_POISONED;
        };
        if host.is_some() {
            return CRAYON_AGENT_HOST_ALREADY_RUNNING;
        }
        match start_macos_uds(
            &purpose,
            port,
            profile,
            config.grant_ttl_ms,
            SchemaVersion::CURRENT,
            capabilities,
        ) {
            Ok((agent, socket_path)) => {
                *host = Some(GlobalHost { agent, socket_path });
                CRAYON_AGENT_HOST_OK
            }
            Err(_) => CRAYON_AGENT_HOST_ENDPOINT_FAILED,
        }
    }))
    .unwrap_or(CRAYON_AGENT_HOST_POISONED);
    result
}

/// Stops the host and joins the serve thread. Idempotent; returns
/// NOT_RUNNING when no host is live.
#[no_mangle]
pub extern "C" fn crayon_agent_host_stop() -> i32 {
    let taken = HOST.lock().ok().and_then(|mut host| host.take());
    match taken {
        Some(global) => {
            global
                .agent
                .stop_and_join(std::time::Duration::from_secs(5));
            CRAYON_AGENT_HOST_OK
        }
        None => CRAYON_AGENT_HOST_NOT_RUNNING,
    }
}

/// Issues a profile-wide grant for the currently connected client — the
/// AGT-05 confirmation outcome. `capability` is a closed capability name.
///
/// # Safety
///
/// `capability` must be a valid NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn crayon_agent_host_issue_grant(capability: *const c_char) -> i32 {
    let capability_name = if capability.is_null() {
        return CRAYON_AGENT_HOST_INVALID_CONFIG;
    } else {
        unsafe { CStr::from_ptr(capability) }
            .to_string_lossy()
            .into_owned()
    };
    let Some(capability) = capability_from_name(&capability_name) else {
        return CRAYON_AGENT_HOST_UNKNOWN_CAPABILITY;
    };
    let Ok(mut host) = HOST.lock() else {
        return CRAYON_AGENT_HOST_POISONED;
    };
    let Some(global) = host.as_mut() else {
        return CRAYON_AGENT_HOST_NOT_RUNNING;
    };
    match global.agent.issue_grant_to_active_client(capability) {
        Ok(()) => CRAYON_AGENT_HOST_OK,
        Err(_) => CRAYON_AGENT_HOST_NOT_RUNNING,
    }
}

/// Writes the hosted endpoint's socket path into |buf| (NUL-terminated).
/// Returns OK, NOT_RUNNING, or INVALID_CONFIG when the path exceeds |cap|.
/// # Safety
///
/// `buf` must be writable for at least |cap| bytes.
#[no_mangle]
pub unsafe extern "C" fn crayon_agent_host_socket_path(buf: *mut c_char, cap: usize) -> i32 {
    let host = HOST.lock().ok();
    let Some(global) = host.as_ref().and_then(|option| option.as_ref()) else {
        return CRAYON_AGENT_HOST_NOT_RUNNING;
    };
    let path = global.socket_path.clone();
    if path.len() + 1 > cap {
        return CRAYON_AGENT_HOST_INVALID_CONFIG;
    }
    if buf.is_null() {
        return CRAYON_AGENT_HOST_INVALID_CONFIG;
    }
    let bytes = path.as_bytes();
    for (offset, byte) in bytes.iter().enumerate() {
        unsafe { *buf.add(offset) = *byte as c_char };
    }
    unsafe { *buf.add(bytes.len()) = 0 };
    CRAYON_AGENT_HOST_OK
}

/// Allocates a host-owned copy of |src| for callback return values.
///
/// # Safety
///
/// `src` must be a valid NUL-terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn crayon_agent_host_string_alloc(src: *const c_char) -> *mut c_char {
    if src.is_null() {
        return std::ptr::null_mut();
    }
    unsafe { CStr::from_ptr(src) }.to_owned().into_raw()
}

/// Releases a string previously returned by `crayon_agent_host_string_alloc`.
///
/// # Safety
///
/// `ptr` must originate from `crayon_agent_host_string_alloc` and must not
/// be freed twice.
#[no_mangle]
pub unsafe extern "C" fn crayon_agent_host_string_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        drop(CString::from_raw(ptr));
    }
}
