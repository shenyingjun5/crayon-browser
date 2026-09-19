//! Current-user Unix Domain Socket agent endpoint (PLT-M04c, AG-012).
//!
//! Binds a UDS at `/tmp/crayon-agent-<purpose>.sock` with peer
//! credentials verified via `getpeereid` — only same-user loopback
//! peers proceed to the handshake.  `stop` unlinks the socket file and
//! is idempotent; `Drop` performs the same teardown, so a hosted
//! endpoint that finishes on its serve thread also unlinks the file.
//! `accept` polls in bounded slices and observes `stop_handle`'s
//! `request_stop`, which lets a host wake a blocked accept without
//! holding the endpoint (AGT-12Ce).

use crate::ffi;
use crayon_platform_api::local_agent_ipc::{
    LocalAgentIpcConnection, LocalAgentIpcEndpoint, LocalAgentIpcError, PeerIdentity,
};
use std::ffi::c_void;
use std::io::{Read, Write};
use std::os::unix::fs::PermissionsExt;
use std::sync::atomic::{AtomicBool, AtomicPtr, Ordering};
use std::sync::Arc;

#[cfg(test)]
#[path = "local_agent_ipc_tests.rs"]
mod tests;

/// Maximum purpose token length, in bytes.
const MAX_PURPOSE_LEN: usize = 64;
/// Listen backlog for the UDS endpoint.
const LISTEN_BACKLOG: i32 = 4;
/// Bounded accept-poll slice: the longest a stop request can go
/// unobserved by a thread blocked in [`MacUdsEndpoint::accept`].
const ACCEPT_POLL_SLICE_MS: i32 = 100;

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

/// Cross-thread stop signal for a hosted endpoint. Cloning keeps both
/// handles pointing at the same flag; `request_stop` wakes a thread
/// blocked in `accept` within one poll slice.
#[derive(Clone)]
pub struct MacUdsStopHandle {
    stop_requested: Arc<AtomicBool>,
}

impl MacUdsStopHandle {
    /// Flags the paired endpoint's accept loop to return `NotRunning`.
    pub fn request_stop(&self) {
        self.stop_requested.store(true, Ordering::Release);
    }
}

/// macOS UDS implementation of the local agent IPC endpoint.
pub struct MacUdsEndpoint {
    purpose: String,
    listen_fd: AtomicPtr<c_void>,
    uid: u64,
    stop_requested: Arc<AtomicBool>,
}

// SAFETY: the raw fd pointer is only accessed from the owning thread;
// the fd has no thread affinity. Contract: the fd is owned by whichever
// thread holds the endpoint (the serve thread once hosted); cross-thread
// shutdown must go through `stop_handle`, never by touching the fd.
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
            stop_requested: Arc::new(AtomicBool::new(false)),
        })
    }

    /// The UDS socket path (diagnostics only; not a secret).
    #[must_use]
    pub fn socket_path(&self) -> String {
        socket_path(&self.purpose)
    }

    /// A handle that wakes this endpoint's accept loop from any thread.
    #[must_use]
    pub fn stop_handle(&self) -> MacUdsStopHandle {
        MacUdsStopHandle {
            stop_requested: Arc::clone(&self.stop_requested),
        }
    }

    /// Accepts one connection and verifies peer credentials via
    /// `getpeereid`.  Wired into the accept loop at AGT-12 transport
    /// assembly; exposed for integration testing.
    pub fn accept_and_check(&self) -> Result<u64, LocalAgentIpcError> {
        let client = self.accept_verified_client()?;
        Ok(client.peer_uid)
    }

    fn accept_verified_client(&self) -> Result<MacVerifiedClient, LocalAgentIpcError> {
        let listen_fd = loop {
            let fd = self.listen_fd.load(Ordering::Acquire);
            if fd.is_null() {
                return Err(LocalAgentIpcError::NotRunning);
            }
            if self.stop_requested.load(Ordering::Acquire) {
                return Err(LocalAgentIpcError::NotRunning);
            }
            // SAFETY: listen_fd is a valid listening socket owned by the
            // endpoint while it is non-null.
            let mut poll_fd = ffi::PollFd {
                fd: fd as i32,
                events: ffi::POLLIN,
                revents: 0,
            };
            // SAFETY: poll_fd outlives the call and count is 1.
            let ready = unsafe { ffi::poll(&mut poll_fd, 1, ACCEPT_POLL_SLICE_MS) };
            if ready < 0 {
                let error = std::io::Error::last_os_error();
                if error.kind() == std::io::ErrorKind::Interrupted {
                    continue;
                }
                return Err(LocalAgentIpcError::HandshakeFailed);
            }
            if ready == 0 {
                continue; // Slice elapsed: re-check the stop conditions.
            }
            if poll_fd.revents & ffi::POLLIN != 0 {
                break fd as i32;
            }
            // A non-POLLIN revents on a listening socket means the fd was
            // invalidated (closed by `stop`) — a clean end of life.
            return Err(LocalAgentIpcError::NotRunning);
        };
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
        let peer = PeerIdentity::new(peer_uid == self.uid, true);
        self.admit_peer(peer)?;
        Ok(MacVerifiedClient {
            fd: peer_fd_guard,
            peer,
            peer_uid,
        })
    }

    /// The stored uid for comparison in tests.
    #[must_use]
    pub fn uid(&self) -> u64 {
        self.uid
    }
}

impl Drop for MacUdsEndpoint {
    fn drop(&mut self) {
        // Same teardown as an explicit stop: a hosted endpoint finishing
        // on its serve thread must not leave the socket file behind.
        let _ = LocalAgentIpcEndpoint::stop(self);
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
        if std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).is_err() {
            let _ = std::fs::remove_file(&path);
            return Err(LocalAgentIpcError::HandshakeFailed);
        }
        // SAFETY: fd is a valid bound socket.
        if unsafe { ffi::listen(fd, LISTEN_BACKLOG) } != 0 {
            let _ = std::fs::remove_file(&path);
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

    fn accept(&self) -> Result<Box<dyn LocalAgentIpcConnection + '_>, LocalAgentIpcError> {
        self.accept_verified_client()
            .map(|client| Box::new(client) as Box<dyn LocalAgentIpcConnection>)
    }
}

struct MacVerifiedClient {
    fd: Fd,
    peer: PeerIdentity,
    peer_uid: u64,
}

impl Read for MacVerifiedClient {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        if self.fd.0 < 0 || buffer.is_empty() {
            return Ok(0);
        }
        // SAFETY: fd is a live connected UDS and the buffer is writable
        // for its declared length.
        let read = unsafe { ffi::read(self.fd.0, buffer.as_mut_ptr(), buffer.len()) };
        if read < 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(read as usize)
    }
}

impl Write for MacVerifiedClient {
    fn write(&mut self, buffer: &[u8]) -> std::io::Result<usize> {
        if self.fd.0 < 0 || buffer.is_empty() {
            return Ok(0);
        }
        // SAFETY: fd is a live connected UDS and the buffer is readable
        // for its declared length.
        let written = unsafe { ffi::write(self.fd.0, buffer.as_ptr(), buffer.len()) };
        if written < 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(written as usize)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl LocalAgentIpcConnection for MacVerifiedClient {
    fn peer_identity(&self) -> PeerIdentity {
        self.peer
    }

    fn close(&mut self) -> Result<(), LocalAgentIpcError> {
        if self.fd.0 >= 0 {
            // SAFETY: fd is exclusively owned by this connection.
            unsafe { ffi::close(self.fd.0) };
            self.fd.0 = -1;
        }
        Ok(())
    }
}
