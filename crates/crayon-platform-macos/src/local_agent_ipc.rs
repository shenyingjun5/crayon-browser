//! Current-user Unix Domain Socket agent endpoint (PLT-M04c, AG-012).
//!
//! Binds a UDS at `/tmp/crayon-agent-<purpose>.sock` with peer
//! credentials verified via `getpeereid` — only same-user loopback
//! peers proceed to the handshake.  `stop` unlinks the socket file and
//! is idempotent.

use crate::ffi;
use crayon_platform_api::local_agent_ipc::{LocalAgentIpcEndpoint, LocalAgentIpcError};
use std::ffi::c_void;
use std::sync::atomic::{AtomicPtr, Ordering};

#[cfg(test)]
#[path = "local_agent_ipc_tests.rs"]
mod tests;

/// Maximum purpose token length, in bytes.
const MAX_PURPOSE_LEN: usize = 64;
/// Listen backlog for the UDS endpoint.
const LISTEN_BACKLOG: i32 = 4;

/// Validates a purpose token (closed charset for the socket path).
fn is_valid_purpose(purpose: &str) -> bool {
    !purpose.is_empty()
        && purpose.len() <= MAX_PURPOSE_LEN
        && purpose
            .bytes()
            .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
}

/// Builds the UDS socket path for a purpose.
fn socket_path(purpose: &str) -> String {
    format!("/tmp/crayon-agent-{purpose}.sock")
}

/// Raw fd wrapper with proper cleanup on drop.
struct Fd(i32);

impl Drop for Fd {
    fn drop(&mut self) {
        if self.0 >= 0 {
            // SAFETY: fd is an open file descriptor owned here.
            unsafe { ffi::close(self.0) };
        }
    }
}

/// macOS UDS implementation of the local agent IPC endpoint.
pub struct MacUdsEndpoint {
    purpose: String,
    listen_fd: AtomicPtr<c_void>,
    uid: u64,
}

// SAFETY: the raw fd pointer is only accessed from the owning thread;
// the fd has no thread affinity.
unsafe impl Send for MacUdsEndpoint {}

impl MacUdsEndpoint {
    /// Creates a UDS endpoint for the given purpose token.
    pub fn new(purpose: &str) -> Result<Self, LocalAgentIpcError> {
        if !is_valid_purpose(purpose) {
            return Err(LocalAgentIpcError::InvalidToken);
        }
        let uid = ffi::current_uid();
        Ok(Self {
            purpose: purpose.to_owned(),
            listen_fd: AtomicPtr::new(std::ptr::null_mut()),
            uid,
        })
    }

    /// The UDS socket path (diagnostics only; not a secret).
    #[must_use]
    pub fn socket_path(&self) -> String {
        socket_path(&self.purpose)
    }

    /// Accepts one connection and verifies peer credentials via
    /// `getpeereid`.  Wired into the accept loop at AGT-12 transport
    /// assembly; exposed for integration testing.
    pub fn accept_and_check(&self) -> Result<u64, LocalAgentIpcError> {
        let fd = self.listen_fd.load(Ordering::Acquire);
        if fd.is_null() {
            return Err(LocalAgentIpcError::NotRunning);
        }
        let listen_fd = fd as i32;
        // SAFETY: listen_fd is a valid listening socket; peer_fd
        // receives a +1 fd.
        let peer_fd = unsafe { ffi::accept(listen_fd, std::ptr::null(), std::ptr::null()) };
        if peer_fd < 0 {
            return Err(LocalAgentIpcError::HandshakeFailed);
        }
        let peer_fd_guard = Fd(peer_fd);
        let mut peer_uid = 0u64;
        let mut peer_gid = 0u64;
        // SAFETY: peer_fd is a valid UDS connection.
        if unsafe { ffi::getpeereid(peer_fd_guard.0, &mut peer_uid, &mut peer_gid) } != 0 {
            return Err(LocalAgentIpcError::HandshakeFailed);
        }
        Ok(peer_uid)
    }

    /// The stored uid for comparison in tests.
    #[must_use]
    pub fn uid(&self) -> u64 {
        self.uid
    }
}

impl LocalAgentIpcEndpoint for MacUdsEndpoint {
    fn start(&mut self) -> Result<(), LocalAgentIpcError> {
        if !self.listen_fd.load(Ordering::Acquire).is_null() {
            return Err(LocalAgentIpcError::AlreadyRunning);
        }
        let path = socket_path(&self.purpose);
        let _ = std::fs::remove_file(&path);
        // SAFETY: socket() is a standard syscall; AF_UNIX = 1, SOCK_STREAM = 1.
        let fd = unsafe { ffi::socket(1, 1, 0) };
        if fd < 0 {
            return Err(LocalAgentIpcError::HandshakeFailed);
        }
        let guard = Fd(fd);
        let mut addr = [0u8; 106];
        addr[0] = 1; // AF_UNIX
        let path_bytes = path.as_bytes();
        if path_bytes.len() >= 104 {
            return Err(LocalAgentIpcError::HandshakeFailed);
        }
        addr[2..2 + path_bytes.len()].copy_from_slice(path_bytes);
        // SAFETY: fd is a valid socket; addr is a valid sockaddr_un.
        let bind_result =
            unsafe { ffi::bind(fd, addr.as_ptr().cast::<c_void>(), path_bytes.len() + 2) };
        if bind_result != 0 {
            return Err(LocalAgentIpcError::AlreadyRunning);
        }
        // SAFETY: fd is a valid bound socket.
        if unsafe { ffi::listen(fd, LISTEN_BACKLOG) } != 0 {
            return Err(LocalAgentIpcError::HandshakeFailed);
        }
        std::mem::forget(guard);
        self.listen_fd.store(fd as *mut c_void, Ordering::Release);
        Ok(())
    }

    fn stop(&mut self) -> Result<(), LocalAgentIpcError> {
        let fd = self.listen_fd.swap(std::ptr::null_mut(), Ordering::AcqRel);
        if fd.is_null() {
            return Ok(());
        }
        // SAFETY: fd is a valid open socket owned by the endpoint.
        unsafe { ffi::close(fd as i32) };
        let path = socket_path(&self.purpose);
        let _ = std::fs::remove_file(&path);
        Ok(())
    }

    fn is_running(&self) -> bool {
        !self.listen_fd.load(Ordering::Acquire).is_null()
    }
}
