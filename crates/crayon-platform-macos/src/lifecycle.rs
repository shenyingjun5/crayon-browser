//! Power/session lifecycle observation over IOKit system-power
//! notifications plus distributed screen-lock notifications
//! (PLT-M04b).
//!
//! Delivered events: `Suspending`/`Resumed` (IOKit power port on a
//! dedicated run-loop thread) and `ScreenLocked`/`ScreenUnlocked`
//! (distributed notifications).  `SessionEnding` has no reliable
//! public macOS source in v1 and is documented as not delivered —
//! `Suspending` already terminates live sessions (CP-004).

use crate::event_relay::{EventRelay, RelaySink};
use crate::ffi;
use crayon_platform_api::lifecycle::{LifecycleError, LifecycleEvent, PowerLifecycleMonitor};
use std::thread::JoinHandle;

#[cfg(test)]
#[path = "lifecycle_tests.rs"]
mod tests;

/// Distributed-notification names for screen lock state.
const SCREEN_LOCKED_NAME: &str = "com.apple.screenIsLocked";
const SCREEN_UNLOCKED_NAME: &str = "com.apple.screenIsUnlocked";

/// Pure mapper from an IOKit interest message type to a lifecycle
/// event.  Sleep messages need the power-cookie acknowledgement, which
/// the callback handles separately.
fn map_io_message(message_type: u32) -> Option<LifecycleEvent> {
    match message_type {
        ffi::K_IO_MESSAGE_SYSTEM_WILL_SLEEP => Some(LifecycleEvent::Suspending),
        ffi::K_IO_MESSAGE_SYSTEM_HAS_POWERED_ON => Some(LifecycleEvent::Resumed),
        _ => None,
    }
}

/// Pure mapper from a distributed-notification name to a lifecycle
/// event.
fn map_distributed_name(name: &str) -> Option<LifecycleEvent> {
    match name {
        SCREEN_LOCKED_NAME => Some(LifecycleEvent::ScreenLocked),
        SCREEN_UNLOCKED_NAME => Some(LifecycleEvent::ScreenUnlocked),
        _ => None,
    }
}

/// macOS implementation of the lifecycle observation contract.
pub struct MacLifecycleMonitor {
    relay: EventRelay<LifecycleEvent>,
    /// Pinned context shared with the IOKit callback and the
    /// distributed-notification observer (owns the sink).
    context: *mut PowerContext,
    run_loop: std::sync::Arc<std::sync::atomic::AtomicPtr<std::ffi::c_void>>,
    worker: Option<JoinHandle<()>>,
}

/// Heap context shared across the OS callbacks; holds the sink and the
/// root power connection (needed to acknowledge sleep).
#[repr(C)]
struct PowerContext {
    sink: Box<RelaySink<LifecycleEvent>>,
    root_connection: *const std::ffi::c_void,
}

// SAFETY: the context is pinned (Box::into_raw) and only mutated from
// the monitor thread; OS callbacks touch the sink whose interior is
// thread-safe.
unsafe impl Send for MacLifecycleMonitor {}

/// SAFETY wrappers for the raw pointers moved into the worker thread:
/// the pinned context outlives the thread (joined in Drop) and the run
/// loop reference is valid for the thread's lifetime.
extern "C" fn power_callback(
    refcon: *mut std::ffi::c_void,
    _service: *const std::ffi::c_void,
    message_type: u32,
    argument: *const std::ffi::c_void,
) {
    // SAFETY: refcon is the pinned PowerContext from registration.
    let context = unsafe { &*(refcon as *const PowerContext) };
    if let Some(event) = map_io_message(message_type) {
        context.sink.push(event);
    }
    if message_type == ffi::K_IO_MESSAGE_SYSTEM_WILL_SLEEP {
        // Acknowledge the sleep so the system can proceed; the sink
        // delivery happens asynchronously.
        let cookie = argument as usize;
        ffi::io_allow_power_change(context.root_connection, cookie);
    }
}

extern "C" fn distributed_callback(
    _center: *const std::ffi::c_void,
    observer: *const std::ffi::c_void,
    name: *const std::ffi::c_void,
    _object: *const std::ffi::c_void,
    _user_info: *const std::ffi::c_void,
) {
    // SAFETY: observer is the pinned PowerContext from registration.
    let context = unsafe { &*(observer as *const PowerContext) };
    // The name arrives as a CFString; describe it through the ffi
    // helper rather than linking CFStringGetLength here.
    if let Some(event) = crate::ffi::describe_cf_name(name).and_then(|n| map_distributed_name(&n)) {
        context.sink.push(event);
    }
}

impl MacLifecycleMonitor {
    /// Creates a monitor, registers the IOKit power callback and the
    /// screen-lock distributed notifications, and starts the delivery
    /// run loop on a dedicated thread.
    pub fn new() -> Result<Self, LifecycleError> {
        let relay = EventRelay::start();
        let sink = Box::new(relay.sink());
        let context = Box::into_raw(Box::new(PowerContext {
            sink,
            root_connection: std::ptr::null(),
        }));
        let (root_connection, notification_port) =
            ffi::io_register_for_system_power(context as *mut std::ffi::c_void, power_callback);
        if root_connection.is_null() {
            // SAFETY: reclaim the pinned context on failure.
            unsafe { drop(Box::from_raw(context)) };
            return Err(LifecycleError::Unavailable);
        }
        // SAFETY: context is pinned; write the connection for the
        // callback's sleep acknowledgement.
        unsafe {
            (*context).root_connection = root_connection;
        }

        // Run loop thread: power source + distributed center delivery.
        ffi::with_distributed_center(|center| {
            for name in [SCREEN_LOCKED_NAME, SCREEN_UNLOCKED_NAME] {
                if let Some(name_cf) = crate::ffi::CfString::new(name) {
                    ffi::cf_notification_add_observer(
                        center,
                        context as *mut std::ffi::c_void,
                        distributed_callback,
                        name_cf.as_ref(),
                    );
                }
            }
        });

        let run_loop_slot =
            std::sync::Arc::new(std::sync::atomic::AtomicPtr::new(std::ptr::null_mut()));
        let run_loop_for_self = std::sync::Arc::clone(&run_loop_slot);
        // SAFETY wrappers: the pinned context and the notification port
        // outlive the worker thread (joined in Drop before either is
        // released); the sink interior is mutex-protected.
        // usize-encoded pointers: usize is Send, eliminating the raw
        // pointer capture issue for the spawned closure.
        let context_addr = context as usize;
        let port_addr = notification_port as usize;
        let run_loop_for_worker = std::sync::Arc::clone(&run_loop_slot);
        let worker = std::thread::Builder::new()
            .name("mac-lifecycle".into())
            .spawn(move || {
                // SAFETY: context_addr and port_addr were derived from
                // pinned allocations that outlive this thread (joined
                // in Drop before either is released).
                #[allow(unused_variables)]
                let context = context_addr as *mut PowerContext;
                let notification_port = port_addr as *mut std::ffi::c_void;
                let source = ffi::io_notification_run_loop_source(notification_port);
                if !source.is_null() {
                    ffi::cf_run_loop_add_source(
                        ffi::with_current_run_loop(|rl| rl),
                        source,
                        ffi::k_cf_run_loop_default_mode(),
                    );
                }
                run_loop_for_worker.store(
                    ffi::with_current_run_loop(|rl| rl) as *mut _,
                    std::sync::atomic::Ordering::Release,
                );
                ffi::cf_run_loop_run();
            })
            .map_err(|_| {
                // SAFETY: reclaim the pinned context on failure.
                unsafe { drop(Box::from_raw(context)) };
                LifecycleError::Unavailable
            })?;
        // Give the thread a moment to record its run loop for Stop.
        std::thread::sleep(std::time::Duration::from_millis(50));
        Ok(Self {
            relay,
            context,
            run_loop: run_loop_for_self,
            worker: Some(worker),
        })
    }
}

impl PowerLifecycleMonitor for MacLifecycleMonitor {
    fn set_listener(
        &mut self,
        listener: Option<Box<dyn FnMut(LifecycleEvent) + Send>>,
    ) -> Result<(), LifecycleError> {
        self.relay.set_listener(listener);
        Ok(())
    }
}

impl Drop for MacLifecycleMonitor {
    fn drop(&mut self) {
        // SAFETY: context was pinned in new() and dropped exactly here.
        unsafe {
            ffi::with_distributed_center(|center| {
                ffi::cf_notification_remove_every_observer(
                    center,
                    self.context as *mut std::ffi::c_void,
                );
            });
            ffi::cf_run_loop_stop(self.run_loop.load(std::sync::atomic::Ordering::Acquire));
            drop(Box::from_raw(self.context));
        }
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
