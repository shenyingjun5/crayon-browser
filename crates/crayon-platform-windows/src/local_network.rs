//! Local-network observation over the Windows IP Helper API (PLT-W04b).
//!
//! Enumeration reports validated adapter names plus up/loopback flags
//! only — never addresses.  Change events come from
//! `NotifyIpInterfaceChange` and `NotifyRouteChange2`, delivered through
//! the bounded [`crate::event_relay`].

use crate::event_relay::{EventRelay, RelaySink};
use crayon_platform_api::local_network::{
    validate_interface_count, InterfaceName, LocalNetworkError, LocalNetworkMonitor,
    NetworkChangeEvent, NetworkInterface,
};
use std::ffi::c_void;
use std::ffi::CStr;

use windows_sys::Win32::Foundation::{ERROR_BUFFER_OVERFLOW, HANDLE};
use windows_sys::Win32::NetworkManagement::IpHelper::{
    CancelMibChangeNotify2, GetAdaptersAddresses, MibAddInstance, MibDeleteInstance,
    MibParameterNotification, NotifyIpInterfaceChange, NotifyRouteChange2,
    GAA_FLAG_INCLUDE_ALL_INTERFACES, IF_TYPE_SOFTWARE_LOOPBACK, IP_ADAPTER_ADDRESSES_LH,
    MIB_IPFORWARD_ROW2, MIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE,
};
use windows_sys::Win32::NetworkManagement::Ndis::IfOperStatusUp;

/// Initial probe buffer for `GetAdaptersAddresses`; grows geometrically.
const INITIAL_ADAPTER_BUFFER: usize = 16 * 1024;
/// Upper retry bound so a pathological stack cannot spin forever.
const MAX_ADAPTER_BUFFER_ATTEMPTS: usize = 5;

/// Raw OS handle wrapper: registration handles are plain pointers, which
/// are not `Send`; cancellation happens on the owning thread in `Drop`
/// while OS callbacks only ever touch the boxed sink, so moving the
/// handle list across threads is sound.
struct NotificationHandle(HANDLE);

// SAFETY: the wrapped HANDLE is an opaque registration token; it carries
// no thread affinity (CancelMibChangeNotify2 accepts any thread) and no
// aliasing references are derived from it.
unsafe impl Send for NotificationHandle {}

/// Windows implementation of the local-network observation contract.
pub struct WindowsNetworkMonitor {
    relay: EventRelay<NetworkChangeEvent>,
    /// Boxed so the OS-callback context address stays stable while `Self`
    /// moves; dropped only after every notification is cancelled.
    sink: Box<RelaySink<NetworkChangeEvent>>,
    notification_handles: Vec<NotificationHandle>,
}

extern "system" fn interface_callback(
    context: *const c_void,
    row: *const MIB_IPINTERFACE_ROW,
    notification_type: MIB_NOTIFICATION_TYPE,
) {
    // SAFETY: `context` points at the `RelaySink` owned by the live
    // monitor; the monitor cancels every notification handle before its
    // sinks are dropped, so no callback can observe freed memory.
    let sink = unsafe { &*(context as *const RelaySink<NetworkChangeEvent>) };
    // SAFETY: the OS passes a valid row for the callback's duration.
    let row = match unsafe { row.as_ref() } {
        Some(row) => row,
        None => return,
    };
    let index = row.InterfaceIndex;
    const MIB_ADD: MIB_NOTIFICATION_TYPE = MibAddInstance;
    const MIB_DELETE: MIB_NOTIFICATION_TYPE = MibDeleteInstance;
    let event = match notification_type {
        // Add/delete carry topology facts; parameter churn (metrics,
        // forwarding flags) is deliberately not surfaced.
        MIB_ADD => NetworkChangeEvent::InterfaceUp(interface_token(index)),
        MIB_DELETE => NetworkChangeEvent::InterfaceDown(interface_token(index)),
        _ => return,
    };
    sink.push(event);
}

extern "system" fn route_callback(
    context: *const c_void,
    _row: *const MIB_IPFORWARD_ROW2,
    notification_type: MIB_NOTIFICATION_TYPE,
) {
    // SAFETY: same lifetime contract as `interface_callback`.
    let sink = unsafe { &*(context as *const RelaySink<NetworkChangeEvent>) };
    if notification_type != MibParameterNotification {
        sink.push(NetworkChangeEvent::DefaultRouteChanged);
    }
}

/// Deterministic closed-charset stand-in name correlated by OS index.
fn interface_token(index: u32) -> InterfaceName {
    // `if-<index>` always satisfies the token charset, so this cannot
    // fail; fall back to a fixed valid token rather than panicking.
    InterfaceName::new(&format!("if-{index}"))
        .unwrap_or_else(|_| InterfaceName::new("if-unknown").expect("valid"))
}

impl WindowsNetworkMonitor {
    /// Creates the monitor and subscribes to interface/route change
    /// notifications.  Subscription failure maps to `Unavailable`.
    pub fn new() -> Result<Self, LocalNetworkError> {
        let relay: EventRelay<NetworkChangeEvent> = EventRelay::start();
        // Heap allocation keeps the callback context address stable even
        // though `Self` itself moves out of this constructor.
        let sink = Box::new(relay.sink());
        let mut monitor = Self {
            relay,
            sink,
            notification_handles: Vec::new(),
        };
        let context = &*monitor.sink as *const RelaySink<NetworkChangeEvent> as *const c_void;
        let mut handle: HANDLE = std::ptr::null_mut();
        // AF_UNSPEC (0): cover IPv4 and IPv6 interface events alike.
        // SAFETY: `context` outlives the registration (see Drop); the
        // handle output pointer is valid for the write.
        let status = unsafe {
            NotifyIpInterfaceChange(0, Some(interface_callback), context, false, &mut handle)
        };
        if status != 0 {
            return Err(LocalNetworkError::Unavailable);
        }
        monitor
            .notification_handles
            .push(NotificationHandle(handle));

        for family in [2u16, 23] {
            // AF_INET / AF_INET6 default-route tables.
            let mut route_handle: HANDLE = std::ptr::null_mut();
            // SAFETY: identical context lifetime contract as above.
            let status = unsafe {
                NotifyRouteChange2(
                    family,
                    Some(route_callback),
                    context,
                    false,
                    &mut route_handle,
                )
            };
            if status != 0 {
                return Err(LocalNetworkError::Unavailable);
            }
            monitor
                .notification_handles
                .push(NotificationHandle(route_handle));
        }
        Ok(monitor)
    }
}

impl LocalNetworkMonitor for WindowsNetworkMonitor {
    fn interfaces(&self) -> Result<Vec<NetworkInterface>, LocalNetworkError> {
        let adapters = enumerate_adapters()?;
        validate_interface_count(adapters.len())?;
        Ok(adapters)
    }

    fn set_listener(
        &mut self,
        listener: Option<Box<dyn FnMut(NetworkChangeEvent) + Send>>,
    ) -> Result<(), LocalNetworkError> {
        self.relay.set_listener(listener);
        Ok(())
    }
}

impl Drop for WindowsNetworkMonitor {
    fn drop(&mut self) {
        // Cancel first: guaranteed no further callbacks, so the boxed
        // sink (and the relay below) stay valid until now.
        for handle in &self.notification_handles {
            // SAFETY: each handle came from a successful registration and
            // is cancelled exactly once here.
            unsafe {
                CancelMibChangeNotify2(handle.0);
            }
        }
        self.notification_handles.clear();
        self.relay.close();
    }
}

/// Enumerates adapters with capability flags only; names are the OS GUID
/// string stripped of braces (closed charset compliant).
fn enumerate_adapters() -> Result<Vec<NetworkInterface>, LocalNetworkError> {
    let mut size = INITIAL_ADAPTER_BUFFER;
    for _attempt in 0..MAX_ADAPTER_BUFFER_ATTEMPTS {
        // Aligned allocation: the adapter list requires pointer alignment.
        let layout = std::alloc::Layout::from_size_align(
            size,
            std::mem::align_of::<IP_ADAPTER_ADDRESSES_LH>(),
        )
        .expect("valid layout");
        // SAFETY: `layout` has non-zero size; the buffer is deallocated on
        // every path below.
        let head = unsafe { std::alloc::alloc_zeroed(layout) };
        let mut written = size as u32;
        // SAFETY: `head` is allocated with the required alignment and
        // holds `size` writable bytes; on overflow the API writes the
        // needed size through `written`.
        let result = unsafe {
            GetAdaptersAddresses(
                0,
                GAA_FLAG_INCLUDE_ALL_INTERFACES,
                std::ptr::null(),
                head.cast(),
                &mut written,
            )
        };
        if result == 0 {
            // SAFETY: the call succeeded and filled `head` with a linked
            // list valid until we free it below.
            let parsed = unsafe { parse_adapter_list(head.cast::<IP_ADAPTER_ADDRESSES_LH>()) };
            // SAFETY: matches the alloc above.
            unsafe { std::alloc::dealloc(head, layout) };
            return parsed;
        }
        // SAFETY: matches the alloc above.
        unsafe { std::alloc::dealloc(head, layout) };
        if result != ERROR_BUFFER_OVERFLOW {
            return Err(LocalNetworkError::Unavailable);
        }
        size *= 2;
    }
    Err(LocalNetworkError::Unavailable)
}

/// Walks the linked list produced by `GetAdaptersAddresses`.
///
/// # Safety
/// `head` must point at a successfully filled adapter list whose entries
/// remain valid while this function runs.
unsafe fn parse_adapter_list(
    head: *mut IP_ADAPTER_ADDRESSES_LH,
) -> Result<Vec<NetworkInterface>, LocalNetworkError> {
    let mut interfaces = Vec::new();
    let mut current = head;
    while !current.is_null() {
        // SAFETY: list nodes are initialised by the OS call above and
        // linked until `Next` is null.
        let adapter = unsafe { &*current };
        let raw_name =
            // SAFETY: `AdapterName` is a NUL-terminated ANSI string owned
            // by the adapter entry.
            unsafe { CStr::from_ptr(adapter.AdapterName as *const std::ffi::c_char) };
        let guid = raw_name.to_string_lossy().replace(['{', '}'], "");
        let name =
            InterfaceName::new(&guid).map_err(|_| LocalNetworkError::InvalidInterfaceName)?;
        interfaces.push(NetworkInterface {
            name,
            is_loopback: adapter.IfType == IF_TYPE_SOFTWARE_LOOPBACK,
            is_up: adapter.OperStatus == IfOperStatusUp,
        });
        current = adapter.Next;
    }
    validate_interface_count(interfaces.len())?;
    Ok(interfaces)
}

#[cfg(test)]
#[path = "local_network_tests.rs"]
mod tests;
