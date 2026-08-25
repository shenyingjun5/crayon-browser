//! Current-user named-pipe agent endpoint (PLT-W04c, AG-012).
//!
//! The listener is created with a DACL granting access to the current
//! user only and with remote clients rejected at the pipe level.  Peer
//! facts are derived from the OS (client process token user compared to
//! the endpoint owner SID); the shared [`LocalAgentIpcEndpoint`] gate
//! then decides admission before any handshake bytes flow.

use crate::ffi::{self};
use crayon_platform_api::local_agent_ipc::{
    LocalAgentIpcEndpoint, LocalAgentIpcError, PeerIdentity,
};
use crayon_platform_api::token::validate_token;
use std::ffi::c_void;
use std::sync::Mutex;

use windows_sys::Win32::Foundation::{CloseHandle, GetLastError, ERROR_ACCESS_DENIED, HANDLE};
use windows_sys::Win32::Security::Authorization::{
    ConvertSidToStringSidW, ConvertStringSecurityDescriptorToSecurityDescriptorW, SDDL_REVISION_1,
};
use windows_sys::Win32::Security::{
    GetTokenInformation, TokenUser, SECURITY_ATTRIBUTES, TOKEN_QUERY, TOKEN_USER,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, FILE_FLAG_FIRST_PIPE_INSTANCE, FILE_SHARE_NONE, OPEN_EXISTING, PIPE_ACCESS_DUPLEX,
};
use windows_sys::Win32::System::Pipes::{
    ConnectNamedPipe, CreateNamedPipeW, DisconnectNamedPipe, GetNamedPipeClientProcessId,
    PIPE_READMODE_BYTE, PIPE_REJECT_REMOTE_CLIENTS, PIPE_TYPE_BYTE,
};
use windows_sys::Win32::System::Threading::{
    GetCurrentProcess, OpenProcess, OpenProcessToken, PROCESS_QUERY_LIMITED_INFORMATION,
};

/// Buffer bound for one framed message batch; mirrors CEF-06 frame caps.
const PIPE_BUFFER_BYTES: u32 = 64 * 1024;

/// Closed start-failure reasons for diagnostics; the trait surface folds
/// these into its stable error set.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EndpointStartFailure {
    /// Another endpoint already owns the pipe name (hijack guard).
    NameInUse,
    /// The OS refused listener creation.
    OsDenied,
}

/// Closed failures of the concrete endpoint beyond the shared gate.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EndpointError {
    /// The shared gate rejected or the endpoint is not running.
    Gate(LocalAgentIpcError),
    /// OS identity observation failed; fail closed.
    OsDenied,
}

/// Wide, NUL-terminated pipe path.
fn pipe_path(purpose: &str) -> Vec<u16> {
    let mut text: Vec<u16> = r"\\.\pipe\crayon-agent-".encode_utf16().collect();
    text.extend(purpose.encode_utf16());
    text.push(0);
    text
}

/// Aligned owned copy of a Windows SID usable as `PSID`.
struct OwnedSid {
    // u32 storage guarantees the alignment `PSID` consumers rely on.
    storage: Box<[u32]>,
}

impl OwnedSid {
    /// Copies `sid` (inside an OS-owned token buffer) into aligned storage.
    ///
    /// # Safety
    /// `sid` must point at a valid SID readable for `GetLengthSid`.
    unsafe fn capture(sid: windows_sys::Win32::Security::PSID) -> Option<Self> {
        // SAFETY: caller guarantees `sid` references a valid SID.
        let length = unsafe { ffi::get_length_sid(sid) };
        if length == 0 || length > 68 {
            // 68 = maximal well-formed SID (15 subauthorities).
            return None;
        }
        let mut storage = vec![0u32; length.div_ceil(4)].into_boxed_slice();
        // SAFETY: destination is u32-aligned and sized per GetLengthSid.
        let ok = unsafe { ffi::copy_sid(storage.as_mut_ptr().cast(), length, sid) };
        if ok == 0 {
            return None;
        }
        Some(Self { storage })
    }

    fn as_ptr(&self) -> windows_sys::Win32::Security::PSID {
        self.storage.as_ptr().cast_mut().cast()
    }
}

/// Raw pipe handle wrapper: the OS registration token carries no thread
/// affinity and no derived references, so moving it across threads is
/// sound; all handle syscalls stay on the owning thread.
struct PipeHandle(HANDLE);

// SAFETY: opaque kernel handle; CloseHandle/DisconnectNamedPipe are called
// only from the thread owning the endpoint state.
unsafe impl Send for PipeHandle {}

/// Current-user named-pipe endpoint.
pub struct WindowsAgentIpcEndpoint {
    pipe_name: Vec<u16>,
    owner_sid: OwnedSid,
    listener: Mutex<Option<PipeHandle>>,
}

impl WindowsAgentIpcEndpoint {
    /// Creates the endpoint description for `purpose` (closed-charset
    /// token) and captures the current user's SID.  No OS resource is
    /// touched until [`start`](LocalAgentIpcEndpoint::start).
    pub fn new(purpose: &str) -> Result<Self, EndpointError> {
        validate_token(purpose, 64)
            .map_err(|_| EndpointError::Gate(LocalAgentIpcError::PeerRejected))?;
        let token = open_current_process_token().ok_or(EndpointError::OsDenied)?;
        // SAFETY: live TOKEN_QUERY handle; capture copies into owned
        // aligned storage before the buffer drops.
        let sid = unsafe { token_user_sid(&token) }.ok_or(EndpointError::OsDenied)?;
        // SAFETY: sid points inside the token buffer captured above.
        let owner_sid = unsafe { OwnedSid::capture(sid) }.ok_or(EndpointError::OsDenied)?;
        Ok(Self {
            pipe_name: pipe_path(purpose),
            owner_sid,
            listener: Mutex::new(None),
        })
    }

    /// Derives peer facts for a client connected on `server_handle` from
    /// OS token data and runs the shared admission gate.  Named pipes are
    /// machine-local (`PIPE_REJECT_REMOTE_CLIENTS`), so the loopback fact
    /// holds by construction.
    ///
    /// # Safety
    /// `server_handle` must be a live listener handle owned by this
    /// endpoint; callers outside [`accept_verified_client`] must uphold
    /// that themselves.
    pub unsafe fn verify_connected_client(
        &self,
        server_handle: HANDLE,
    ) -> Result<bool, EndpointError> {
        let mut pid = 0u32;
        // SAFETY: `server_handle` is the live listener handle returned by
        // CreateNamedPipeW; `pid` receives the client id.
        let ok = unsafe { GetNamedPipeClientProcessId(server_handle, &mut pid) };
        if ok == 0 || pid == 0 {
            return Err(EndpointError::OsDenied);
        }
        // SAFETY: read-only query on the client process identified by the
        // pipe infrastructure itself.
        let process = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
        if process.is_null() {
            // Cannot observe the peer: fail closed.
            return Ok(false);
        }
        let facts = (|| {
            let token = open_token(process)?;
            // SAFETY: live TOKEN_QUERY handle from OpenProcessToken.
            let sid = unsafe { token_user_sid(&token) }?;
            // SAFETY: both pointers reference valid SIDs captured above.
            Some(unsafe { ffi::equal_sid(self.owner_sid.as_ptr(), sid) != 0 })
        })()
        .unwrap_or(false);
        // SAFETY: handle acquired by OpenProcess above.
        unsafe { CloseHandle(process) };
        Ok(facts)
    }

    /// Returns the pipe path clients connect to (diagnostic only).
    #[must_use]
    pub fn pipe_name_lossy(&self) -> String {
        String::from_utf16_lossy(&self.pipe_name[..self.pipe_name.len() - 1])
    }

    /// Wide pipe path for [`connect_client`].
    #[must_use]
    pub fn pipe_path_for_connect(&self) -> Vec<u16> {
        self.pipe_name.clone()
    }

    /// Blocks until one client connects, then admits or rejects it on OS
    /// token facts.  Rejected connections are disconnected before the
    /// error returns; nothing about the peer reaches the error payload.
    pub fn accept_verified_client(&self) -> Result<VerifiedClient, EndpointError> {
        let handle = {
            let slot = self.listener.lock().expect("listener mutex");
            slot.as_ref()
                .map(|pipe| pipe.0)
                .ok_or(EndpointError::Gate(LocalAgentIpcError::NotRunning))?
        };
        // SAFETY: live listener handle cloned out of the slot; the slot
        // keeps its own reference until stop().
        let connected = unsafe { ConnectNamedPipe(handle, std::ptr::null_mut()) != 0 };
        // A client that connected between creation and ConnectNamedPipe
        // yields ERROR_PIPE_CONNECTED; treat that as connected too.
        // ERROR_PIPE_CONNECTED: the client connected between creation
        // and ConnectNamedPipe.
        // SAFETY: plain TLS error read after the ConnectNamedPipe call.
        let already = (unsafe { GetLastError() }) == 536;
        if !connected && !already {
            return Err(EndpointError::OsDenied);
        }
        // SAFETY: handle cloned from the live listener slot above.
        match unsafe { self.verify_connected_client(handle) }? {
            true => Ok(VerifiedClient {
                handle: PipeHandle(handle),
            }),
            false => {
                // SAFETY: reject closes our view of the peer session.
                unsafe { DisconnectNamedPipe(handle) };
                Err(EndpointError::Gate(LocalAgentIpcError::PeerRejected))
            }
        }
    }

    fn create_listener(&self) -> Result<HANDLE, EndpointStartFailure> {
        let descriptor =
            build_owner_only_descriptor(&self.owner_sid).ok_or(EndpointStartFailure::OsDenied)?;
        let attributes = SECURITY_ATTRIBUTES {
            nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: descriptor,
            bInheritHandle: 0,
        };
        // SAFETY: `pipe_name` and `attributes` outlive the call; the
        // FIRST_PIPE_INSTANCE flag fails closed when the name is taken.
        let handle = unsafe {
            CreateNamedPipeW(
                self.pipe_name.as_ptr(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
                1,
                PIPE_BUFFER_BYTES,
                PIPE_BUFFER_BYTES,
                0,
                &attributes,
            )
        };
        // SAFETY: the descriptor was allocated by the conversion call.
        unsafe { ffi::local_free(descriptor) };
        if handle.is_null() || handle == (-1isize as HANDLE) {
            // SAFETY: plain TLS error read after the failing call.
            return match unsafe { GetLastError() } {
                ERROR_ACCESS_DENIED => Err(EndpointStartFailure::NameInUse),
                _ => Err(EndpointStartFailure::OsDenied),
            };
        }
        Ok(handle)
    }
}

impl LocalAgentIpcEndpoint for WindowsAgentIpcEndpoint {
    fn start(&mut self) -> Result<(), LocalAgentIpcError> {
        let mut slot = self.listener.lock().expect("listener mutex");
        if slot.is_some() {
            return Err(LocalAgentIpcError::AlreadyRunning);
        }
        match self.create_listener() {
            Ok(handle) => {
                *slot = Some(PipeHandle(handle));
                Ok(())
            }
            Err(failure) => {
                let _ = failure;
                // Postcondition truth: nothing is listening.  Cause detail
                // stays in `create_listener` for diagnostic wiring.
                Err(LocalAgentIpcError::NotRunning)
            }
        }
    }

    fn stop(&mut self) -> Result<(), LocalAgentIpcError> {
        let mut slot = self.listener.lock().expect("listener mutex");
        if let Some(slot_handle) = slot.take() {
            // SAFETY: sole ownership of the listener handle.
            unsafe {
                DisconnectNamedPipe(slot_handle.0);
                CloseHandle(slot_handle.0);
            }
        }
        Ok(())
    }

    fn is_running(&self) -> bool {
        self.listener.lock().expect("listener mutex").is_some()
    }

    fn admit_peer(&self, peer: PeerIdentity) -> Result<(), LocalAgentIpcError> {
        {
            let slot = self.listener.lock().expect("listener mutex");
            if slot.is_none() {
                return Err(LocalAgentIpcError::NotRunning);
            }
        }
        // Shared AG-012 conjunction: same_user ∧ loopback, else the peer
        // is rejected before any handshake byte flows.
        match peer.handshake_allowed() {
            true => Ok(()),
            false => Err(LocalAgentIpcError::PeerRejected),
        }
    }
}

/// A peer whose OS user identity passed the AG-012 gate.  The raw pipe
/// handle is co-owned by the endpoint slot and the transport; transports
/// close it exactly once when the session ends.
pub struct VerifiedClient {
    handle: PipeHandle,
}

impl VerifiedClient {
    /// Raw pipe handle for framed I/O; never closed through this view.
    #[must_use]
    pub fn raw(&self) -> HANDLE {
        self.handle.0
    }
}

/// Opens an existing pipe end as a client; used by the CAAP transport and
/// integration tests to reach the endpoint over the real named pipe.
///
/// # Safety
/// `path_wide` must be NUL-terminated UTF-16; the returned handle must be
/// closed exactly once.
pub unsafe fn connect_client(path_wide: &[u16]) -> HANDLE {
    // SAFETY: `path_wide` is NUL-terminated UTF-16.
    unsafe {
        CreateFileW(
            path_wide.as_ptr(),
            PIPE_ACCESS_DUPLEX,
            FILE_SHARE_NONE,
            std::ptr::null(),
            OPEN_EXISTING,
            0,
            std::ptr::null_mut(),
        )
    }
}

// ---------------------------------------------------------------------------
// helpers

struct OwnedToken(HANDLE);

impl Drop for OwnedToken {
    fn drop(&mut self) {
        // SAFETY: token opened by OpenProcessToken above.
        unsafe { CloseHandle(self.0) };
    }
}

fn open_current_process_token() -> Option<OwnedToken> {
    // SAFETY: pseudo-handle constant, never closed.
    let process = unsafe { GetCurrentProcess() };
    open_token(process)
}

fn open_token(process: HANDLE) -> Option<OwnedToken> {
    let mut token: HANDLE = std::ptr::null_mut();
    // SAFETY: live process handle; token output written through the pointer.
    let ok = unsafe { OpenProcessToken(process, TOKEN_QUERY, &mut token) };
    if ok == 0 || token.is_null() {
        return None;
    }
    Some(OwnedToken(token))
}

/// Extracts the user SID pointer from a query-sized token buffer.
///
/// # Safety
/// `token` must be a live TOKEN_QUERY handle.
unsafe fn token_user_sid(token: &OwnedToken) -> Option<windows_sys::Win32::Security::PSID> {
    let mut needed = 0u32;
    // SAFETY: size probe with null buffer is the documented pattern.
    unsafe {
        GetTokenInformation(token.0, TokenUser, std::ptr::null_mut(), 0, &mut needed);
    }
    if needed == 0 {
        return None;
    }
    let mut buffer = vec![0u8; needed as usize];
    // SAFETY: buffer sized by the probe; TOKEN_USER layout is fixed.
    let ok = unsafe {
        GetTokenInformation(
            token.0,
            TokenUser,
            buffer.as_mut_ptr().cast::<c_void>(),
            needed,
            &mut needed,
        )
    };
    if ok == 0 {
        return None;
    }
    // SAFETY: buffer now holds a TOKEN_USER whose User.Sid points inside
    // it; lifetime covers the caller's SID capture.
    let user = unsafe { &*(buffer.as_ptr() as *const TOKEN_USER) };
    Some(user.User.Sid)
}

/// Builds `D:P(A;;GA;;;<owner-sid>)` as an OS security descriptor.
fn build_owner_only_descriptor(owner: &OwnedSid) -> Option<*mut c_void> {
    let mut sid_string: windows_sys::core::PWSTR = std::ptr::null_mut();
    // SAFETY: owner SID is valid; the string is freed below.
    let ok = unsafe { ConvertSidToStringSidW(owner.as_ptr(), &mut sid_string) };
    if ok == 0 || sid_string.is_null() {
        return None;
    }
    let mut length = 0usize;
    // SAFETY: walking a NUL-terminated wide string returned by the OS
    // conversion; reads stop at the terminator and stay in bounds.
    while unsafe { *sid_string.add(length) } != 0 {
        length += 1;
    }
    // SAFETY: `length` elements were validated by the scan above.
    let sid_text =
        unsafe { String::from_utf16_lossy(std::slice::from_raw_parts(sid_string, length)) };
    // SAFETY: free the OS string right after copying.
    unsafe { ffi::local_free(sid_string.cast::<c_void>()) };

    let sddl: Vec<u16> = format!("D:P(A;;GA;;;{sid_text})\0")
        .encode_utf16()
        .collect();
    let mut descriptor: *mut c_void = std::ptr::null_mut();
    // SAFETY: outputs through `descriptor`; revision constant documented.
    let ok = unsafe {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.as_ptr(),
            SDDL_REVISION_1,
            &mut descriptor,
            std::ptr::null_mut(),
        )
    };
    if ok == 0 {
        None
    } else {
        Some(descriptor)
    }
}

#[cfg(test)]
#[path = "local_agent_ipc_tests.rs"]
mod tests;
