//! Local-network observation over `getifaddrs` + a PF_ROUTE socket
//! (PLT-M04b).
//!
//! Enumeration reports validated interface names plus up/loopback flags
//! only — never addresses.  Change events come from a raw PF_ROUTE
//! socket read on a dedicated thread: `RTM_IFINFO` maps to
//! InterfaceUp/Down via the interface flags, and default-route
//! add/delete messages map to `DefaultRouteChanged` (CP-004: any route
//! change is a reason to require a fresh user-initiated reconnect).

use crate::event_relay::{EventRelay, RelaySink};
use crate::ffi;
use crayon_platform_api::local_network::{
    validate_interface_count, InterfaceName, LocalNetworkError, LocalNetworkMonitor,
    NetworkChangeEvent, NetworkInterface, MAX_INTERFACES,
};
use std::thread::JoinHandle;

#[cfg(test)]
#[path = "local_network_tests.rs"]
mod tests;

/// Upper bound for a single kernel routing message.
const ROUTE_MSG_BUFFER: usize = 2048;

/// macOS implementation of the local-network observation contract.
///
/// SAFETY: the raw sink pointer and route socket fd are managed with
/// proper lifetime guarantees (Drop joins the reader before dropping
/// the boxed sink); the sink's interior is mutex-protected.
pub struct MacNetworkMonitor {
    relay: EventRelay<NetworkChangeEvent>,
    /// Boxed so the OS-callback/context addresses stay stable while
    /// `Self` moves; dropped only after the reader thread exits.
    sink: Box<RelaySink<NetworkChangeEvent>>,
    route_fd: i32,
    wakeup_fd: i32,
    reader: Option<JoinHandle<()>>,
}

impl MacNetworkMonitor {
    /// Creates a monitor and starts the routing-message reader thread.
    /// The socket is opened best-effort: a sandboxed or restricted
    /// environment degrades to enumeration-only with change events
    /// unavailable (fail-closed, no spurious events).
    pub fn new() -> Result<Self, LocalNetworkError> {
        let relay = EventRelay::start();
        let sink = Box::new(relay.sink());
        // SAFETY: socket() is a standard syscall with valid args.
        let route_fd = unsafe { ffi::socket(ffi::PF_ROUTE, ffi::SOCK_RAW, 0) };
        let (route_fd, wakeup_fd, reader) = if route_fd >= 0 {
            let mut fds = [0i32; 2];
            // SAFETY: pipe() writes two fds into the valid array.
            let pipe_ok = unsafe { ffi::pipe(fds.as_mut_ptr()) } == 0;
            if pipe_ok {
                let [read_fd, write_fd] = fds;
                // usize-encoded pointer: usize is Send.
                let sink_addr =
                    std::ptr::from_ref::<RelaySink<NetworkChangeEvent>>(&*sink) as usize;
                let reader = std::thread::Builder::new()
                    .name("mac-route-reader".into())
                    .spawn(move || {
                        // SAFETY: sink_addr was derived from the boxed
                        // sink that outlives this thread (joined in
                        // Drop before the box drops).
                        // SAFETY: sink_addr was derived from the boxed
                        // sink that outlives this thread.
                        let sink = unsafe { &*(sink_addr as *const RelaySink<NetworkChangeEvent>) };
                        Self::read_loop(sink, route_fd, read_fd);
                    })
                    .expect("spawn route reader");
                (route_fd, write_fd, Some(reader))
            } else {
                // No wakeup pipe: enumeration-only mode.
                // SAFETY: route_fd is an open socket we own.
                unsafe { ffi::close(route_fd) };
                (-1, -1, None)
            }
        } else {
            (-1, -1, None)
        };
        Ok(Self {
            relay,
            sink,
            route_fd,
            wakeup_fd,
            reader,
        })
    }

    /// Reader loop: polls the route socket and the wakeup pipe; exits
    /// on stop or socket error.
    fn read_loop(sink: &RelaySink<NetworkChangeEvent>, route_fd: i32, wakeup_fd: i32) {
        let mut buffer = [0u8; ROUTE_MSG_BUFFER];
        loop {
            let mut fds = [
                ffi::PollFd {
                    fd: route_fd,
                    events: ffi::POLLIN,
                    revents: 0,
                },
                ffi::PollFd {
                    fd: wakeup_fd,
                    events: ffi::POLLIN,
                    revents: 0,
                },
            ];
            // SAFETY: fds is a valid two-element poll array.
            let ready = unsafe { ffi::poll(fds.as_mut_ptr(), 2, -1) };
            if ready <= 0 {
                continue; // EINTR or spurious wakeup: re-poll
            }
            if fds[1].revents & ffi::POLLIN != 0 {
                return; // stop requested
            }
            if fds[0].revents & ffi::POLLIN == 0 {
                continue;
            }
            // SAFETY: route_fd is an open raw socket owned by this
            // thread; buffer has room for one kernel message.
            // SAFETY: route_fd is an open socket; buffer has room.
            let n = unsafe { ffi::read(route_fd, buffer.as_mut_ptr(), buffer.len()) };
            if n <= 0 {
                return; // socket closed or error: exit
            }
            Self::dispatch_route_message(sink, &buffer[..n as usize]);
        }
    }

    /// Parses one kernel routing message (possibly several concatenated
    /// bytes are handled by the caller looping) and pushes mapped
    /// events.  Malformed bytes are skipped silently — the kernel never
    /// sends malformed messages, so silence is fail-closed.
    fn dispatch_route_message(sink: &RelaySink<NetworkChangeEvent>, bytes: &[u8]) {
        let mut offset = 0usize;
        while offset + std::mem::size_of::<ffi::RtMsghdr>() <= bytes.len() {
            let header = unsafe {
                // SAFETY: offset + size_of::<RtMsghdr>() is within
                // bounds per the loop condition; alignment 1 via
                // read_unaligned-equivalent struct copy.
                std::ptr::read_unaligned(bytes.as_ptr().add(offset).cast::<ffi::RtMsghdr>())
            };
            if header.rtm_version != 3 {
                break; // unknown version: stop scanning this read
            }
            let msg_len = header.rtm_msglen as usize;
            if msg_len < std::mem::size_of::<ffi::RtMsghdr>() || offset + msg_len > bytes.len() {
                break; // truncated: stop scanning
            }
            let payload = &bytes[offset..offset + msg_len];
            if let Some(event) = Self::map_message(&header, payload) {
                sink.push(event);
            }
            offset += msg_len;
        }
    }

    /// Pure mapper from one routing message to a change event.
    fn map_message(header: &ffi::RtMsghdr, payload: &[u8]) -> Option<NetworkChangeEvent> {
        match header.rtm_type {
            ffi::RTM_IFINFO => {
                // SAFETY: RTM_IFINFO payloads carry `struct if_msghdr`
                // right after the rt_msghdr header; the caller bounds
                // the slice to rtm_msglen.
                let ifm = unsafe {
                    std::ptr::read_unaligned(
                        payload
                            .as_ptr()
                            .add(std::mem::size_of::<ffi::RtMsghdr>())
                            .cast::<ffi::IfMsghdr>(),
                    )
                };
                let index = ifm.ifm_index;
                let up = (ifm.ifm_flags as u32) & ffi::IFF_UP != 0;
                let name = InterfaceName::new(&format!("if{index}")).ok()?;
                Some(if up {
                    NetworkChangeEvent::InterfaceUp(name)
                } else {
                    NetworkChangeEvent::InterfaceDown(name)
                })
            }
            ffi::RTM_ADD | ffi::RTM_DELETE => {
                if Self::is_default_route(payload) {
                    Some(NetworkChangeEvent::DefaultRouteChanged)
                } else {
                    None
                }
            }
            _ => None,
        }
    }

    /// Reports whether an RTM_ADD/RTM_DELETE payload carries a default
    /// route: the address list has no destination, or the destination
    /// is all zeroes.
    fn is_default_route(payload: &[u8]) -> bool {
        let header_size = std::mem::size_of::<ffi::RtMsghdr>();
        if payload.len() < header_size + 8 {
            return false;
        }
        // SAFETY: header bounds checked above.
        let addrs = unsafe { std::ptr::read_unaligned(payload.as_ptr().add(12).cast::<i32>()) };
        if addrs & ffi::RTA_DST == 0 {
            return true; // no destination: default route
        }
        // Walk the sockaddr chain: first entry is the destination.
        let pos = header_size;
        // Bounded reads below; sockaddr_len is the first byte (min 1).
        let Some(first) = payload.get(pos) else {
            return false;
        };
        let sa_len = ((*first as usize) + 1).max(1);
        let Some(dst) = payload.get(pos..pos + sa_len) else {
            return false;
        };
        // All-zero beyond the (len, family) header means the
        // unspecified address = default route.
        dst[2..].iter().all(|b| *b == 0)
    }
}

impl LocalNetworkMonitor for MacNetworkMonitor {
    fn interfaces(&self) -> Result<Vec<NetworkInterface>, LocalNetworkError> {
        let mut ifap: *const ffi::IfAddrs = std::ptr::null();
        // SAFETY: ifap receives a +1 linked list freed by freeifaddrs.
        // SAFETY: ifap receives a framework-allocated linked list.
        let status = unsafe { ffi::getifaddrs(&mut ifap) };
        if status != 0 {
            return Err(LocalNetworkError::Unavailable);
        }
        let mut result = Vec::new();
        let mut seen: Vec<String> = Vec::new();
        let mut cursor = ifap;
        while !cursor.is_null() && result.len() < MAX_INTERFACES {
            // SAFETY: walking the framework-allocated list; each node
            // is valid until freeifaddrs.
            unsafe {
                let name_c = (*cursor).ifa_name;
                if name_c.is_null() {
                    cursor = (*cursor).ifa_next;
                    continue;
                }
                let mut len = 0usize;
                while *name_c.add(len) != 0 && len < 128 {
                    len += 1;
                }
                let name = std::str::from_utf8(std::slice::from_raw_parts(name_c, len))
                    .unwrap_or_default();
                if !name.is_empty() && !seen.contains(&name.to_string()) {
                    if let Ok(validated) = InterfaceName::new(name) {
                        seen.push(name.to_string());
                        let flags = (*cursor).ifa_flags;
                        result.push(NetworkInterface {
                            name: validated,
                            is_loopback: flags & ffi::IFF_LOOPBACK != 0,
                            is_up: flags & ffi::IFF_UP != 0,
                        });
                    }
                }
                cursor = (*cursor).ifa_next;
            }
        }
        // SAFETY: +1 list from getifaddrs.
        unsafe { ffi::freeifaddrs(ifap) };
        validate_interface_count(result.len())?;
        Ok(result)
    }

    fn set_listener(
        &mut self,
        listener: Option<Box<dyn FnMut(NetworkChangeEvent) + Send>>,
    ) -> Result<(), LocalNetworkError> {
        self.relay.set_listener(listener);
        Ok(())
    }
}

impl Drop for MacNetworkMonitor {
    fn drop(&mut self) {
        // The sink Box is dropped after the reader joins, keeping the
        // raw pointer valid for the thread's entire lifetime.
        let _ = &self.sink;
        // Wake the reader thread, then join it before releasing the
        // socket and sink.
        if self.wakeup_fd >= 0 {
            let byte = [1u8];
            // SAFETY: wakeup_fd is an open pipe write end.
            unsafe { ffi::write(self.wakeup_fd, byte.as_ptr(), 1) };
        }
        if let Some(reader) = self.reader.take() {
            let _ = reader.join();
        }
        if self.route_fd >= 0 {
            // SAFETY: route_fd is an open socket owned by this monitor.
            unsafe { ffi::close(self.route_fd) };
        }
        if self.wakeup_fd >= 0 {
            // SAFETY: open pipe write end owned by this monitor.
            unsafe { ffi::close(self.wakeup_fd) };
        }
    }
}
