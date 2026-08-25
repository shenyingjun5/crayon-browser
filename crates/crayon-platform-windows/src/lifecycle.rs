//! Power/session lifecycle observation over the Windows message pump
//! (PLT-W04b).
//!
//! A dedicated thread owns a message-only window receiving
//! `WM_POWERBROADCAST` (suspend/resume), `WM_WTSSESSION_CHANGE`
//! (lock/unlock) and `WM_ENDSESSION`.  Events flow through the bounded
//! [`crate::event_relay`]; CP-004 re-validation belongs to session owners.

use crate::event_relay::{EventRelay, RelaySink};
use crayon_platform_api::lifecycle::{LifecycleError, LifecycleEvent, PowerLifecycleMonitor};
use std::ffi::c_void;
use std::sync::mpsc;

use windows_sys::Win32::Foundation::{
    GetLastError, ERROR_CLASS_ALREADY_EXISTS, HWND, LPARAM, LRESULT, WPARAM,
};
use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
use windows_sys::Win32::System::RemoteDesktop::{
    WTSRegisterSessionNotification, WTSUnRegisterSessionNotification, NOTIFY_FOR_THIS_SESSION,
};
use windows_sys::Win32::System::Threading::GetCurrentThreadId;
use windows_sys::Win32::UI::WindowsAndMessaging as msg;
use windows_sys::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, GetMessageW,
    GetWindowLongPtrW, PostQuitMessage, PostThreadMessageW, RegisterClassW, SetWindowLongPtrW,
    TranslateMessage, CREATESTRUCTW, WM_CREATE, WM_DESTROY, WM_ENDSESSION, WNDCLASSW,
    WTS_SESSION_LOCK, WTS_SESSION_UNLOCK,
};

/// Window class name; unique enough for a process-local message-only
/// window.
const CLASS_NAME: &[u16] = &[
    b'c' as u16,
    b'r' as u16,
    b'a' as u16,
    b'y' as u16,
    b'o' as u16,
    b'n' as u16,
    b'-' as u16,
    b'l' as u16,
    b'i' as u16,
    b'f' as u16,
    b'e' as u16,
    b'c' as u16,
    b'y' as u16,
    b'c' as u16,
    b'l' as u16,
    b'e' as u16,
    0,
];

/// `GWLP_USERDATA` slot holding the relay-sink pointer.
const GWLP_USERDATA: i32 = -20;
/// Parent handle for message-only windows (`HWND_MESSAGE`).
const HWND_MESSAGE: HWND = -3isize as HWND;

/// Raw sink pointer crossing into the pump thread; the pointee is owned
/// by the pump loop alone and freed there, so sending it is sound.
struct PumpSink(*mut RelaySink<LifecycleEvent>);
// SAFETY: sole ownership transfers with the value; no other thread reads
// the pointer until the pump thread tears the window down.
unsafe impl Send for PumpSink {}

/// Windows implementation of the power/session lifecycle contract.
pub struct WindowsLifecycleMonitor {
    relay: EventRelay<LifecycleEvent>,
    pump: Option<std::thread::JoinHandle<()>>,
    pump_thread_id: u32,
}

/// Handshake from the pump thread back to the constructor.
enum PumpReady {
    Ready { thread_id: u32 },
    Failed,
}

impl WindowsLifecycleMonitor {
    /// Starts the message-pump thread and waits (bounded) for window
    /// creation and session-notification registration.
    pub fn new() -> Result<Self, LifecycleError> {
        let relay: EventRelay<LifecycleEvent> = EventRelay::start();
        let (tx, rx) = mpsc::channel();
        // The raw sink travels to WM_CREATE and lives until the pump loop
        // tears the window down; see `pump_loop`.
        let sink_ptr = Box::into_raw(Box::new(relay.sink()));
        let pump_sink = PumpSink(sink_ptr);
        let pump = std::thread::Builder::new()
            .name("crayon-lifecycle-pump".into())
            .spawn(move || pump_loop(pump_sink, tx))
            .map_err(|_| LifecycleError::Unavailable)?;
        match rx
            .recv_timeout(std::time::Duration::from_secs(5))
            .unwrap_or(PumpReady::Failed)
        {
            PumpReady::Ready { thread_id } => Ok(Self {
                relay,
                pump: Some(pump),
                pump_thread_id: thread_id,
            }),
            PumpReady::Failed => Err(LifecycleError::Unavailable),
        }
    }
}

impl PowerLifecycleMonitor for WindowsLifecycleMonitor {
    fn set_listener(
        &mut self,
        listener: Option<Box<dyn FnMut(LifecycleEvent) + Send>>,
    ) -> Result<(), LifecycleError> {
        self.relay.set_listener(listener);
        Ok(())
    }
}

impl Drop for WindowsLifecycleMonitor {
    fn drop(&mut self) {
        self.relay.close();
        // SAFETY: `pump_thread_id` identifies the live pump thread; posting
        // WM_QUIT is the documented cross-thread message-loop exit.
        unsafe {
            PostThreadMessageW(self.pump_thread_id, msg::WM_QUIT, 0, 0);
        }
        if let Some(handle) = self.pump.take() {
            let _ = handle.join();
        }
    }
}

/// Maps one window message to its lifecycle event, if any.
fn map_message(message: u32, wparam: WPARAM) -> Option<LifecycleEvent> {
    // WPARAM is usize in windows-sys while the PBT_/WTS_ payload constants
    // are u32; normalise once here.
    let code = wparam as u32;
    match message {
        msg::WM_POWERBROADCAST => match code {
            msg::PBT_APMSUSPEND => Some(LifecycleEvent::Suspending),
            msg::PBT_APMRESUMEAUTOMATIC | msg::PBT_APMRESUMESUSPEND => {
                Some(LifecycleEvent::Resumed)
            }
            _ => None,
        },
        WM_ENDSESSION if wparam != 0 => Some(LifecycleEvent::SessionEnding),
        msg::WM_WTSSESSION_CHANGE => match code {
            WTS_SESSION_LOCK => Some(LifecycleEvent::ScreenLocked),
            WTS_SESSION_UNLOCK => Some(LifecycleEvent::ScreenUnlocked),
            _ => None,
        },
        _ => None,
    }
}

unsafe extern "system" fn window_proc(
    hwnd: HWND,
    message: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    if message == WM_CREATE {
        // SAFETY: during WM_CREATE, `lparam` points at the CREATESTRUCTW
        // whose lpCreateParams carries the sink pointer we passed to
        // CreateWindowExW.
        let create = unsafe { &*(lparam as *const CREATESTRUCTW) };
        // SAFETY: fresh live window; the slot is written once here.
        unsafe {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, create.lpCreateParams as isize);
        }
        return 0;
    }
    if message == WM_DESTROY {
        // SAFETY: posts to this thread's queue.
        // SAFETY: posts to this thread's message queue.
        unsafe { PostQuitMessage(0) };
        return 0;
    }
    if let Some(event) = map_message(message, wparam) {
        // SAFETY: the slot was set in WM_CREATE before any lifecycle
        // message can be dispatched, and cleared only at teardown.
        let pointer = unsafe { GetWindowLongPtrW(hwnd, GWLP_USERDATA) };
        if pointer != 0 {
            // SAFETY: the pointer targets the sink box owned by the pump
            // loop, freed only after this window is destroyed.
            let sink = unsafe { &*(pointer as *const RelaySink<LifecycleEvent>) };
            sink.push(event);
        }
        return 0;
    }
    // SAFETY: plain fallthrough to the default procedure.
    unsafe { DefWindowProcW(hwnd, message, wparam, lparam) }
}

fn pump_loop(sink: PumpSink, ready: mpsc::Sender<PumpReady>) {
    let sink_ptr = sink.0;
    // SAFETY: null module handle returns the executable's HMODULE.
    let hinstance = unsafe { GetModuleHandleW(std::ptr::null()) };
    // SAFETY: `CLASS_NAME` is NUL-terminated; the class is registered once
    // for this process-local message-only window.
    let class = WNDCLASSW {
        style: 0,
        lpfnWndProc: Some(window_proc),
        cbClsExtra: 0,
        cbWndExtra: 0,
        hInstance: hinstance,
        hIcon: std::ptr::null_mut(),
        hCursor: std::ptr::null_mut(),
        hbrBackground: std::ptr::null_mut(),
        lpszMenuName: std::ptr::null(),
        lpszClassName: CLASS_NAME.as_ptr(),
    };
    // SAFETY: `class` is a fully initialised stack WNDCLASSW.
    let atom = unsafe { RegisterClassW(&class) };
    if atom == 0 {
        // Parallel monitors share the process-wide class registry; an
        // already-registered class is fine and proceeds to window creation.
        // SAFETY: plain TLS error read after the failing call.
        let exists = unsafe { GetLastError() } == ERROR_CLASS_ALREADY_EXISTS;
        if !exists {
            teardown_failed(sink_ptr, ready);
            return;
        }
    }
    // SAFETY: message-only window under HWND_MESSAGE; lpParam forwards the
    // sink pointer to WM_CREATE.
    let hwnd = unsafe {
        CreateWindowExW(
            0,
            CLASS_NAME.as_ptr(),
            std::ptr::null(),
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            std::ptr::null_mut(),
            hinstance,
            sink_ptr.cast::<c_void>(),
        )
    };
    if hwnd.is_null() {
        teardown_failed(sink_ptr, ready);
        return;
    }
    // SAFETY: `hwnd` is the freshly created live window of this session.
    if unsafe { WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION) } == 0 {
        // SAFETY: window exists; destroying before freeing the sink keeps
        // the wndproc from observing freed memory.
        unsafe { DestroyWindow(hwnd) };
        teardown_failed(sink_ptr, ready);
        return;
    }
    let _ = ready.send(PumpReady::Ready {
        // SAFETY: called on the pump thread itself.
        thread_id: unsafe { GetCurrentThreadId() },
    });

    let mut message = msg::MSG {
        hwnd: std::ptr::null_mut(),
        message: 0,
        wParam: 0,
        lParam: 0,
        time: 0,
        pt: Default::default(),
    };
    // SAFETY: standard message loop; GetMessageW returns <= 0 on WM_QUIT
    // or error, ending delivery before the sink is freed below.
    unsafe {
        while GetMessageW(&mut message, std::ptr::null_mut(), 0, 0) > 0 {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        WTSUnRegisterSessionNotification(hwnd);
        DestroyWindow(hwnd);
        drop(Box::from_raw(sink_ptr));
    }
}

/// Failure path: releases the sink and reports setup failure.
fn teardown_failed(sink_ptr: *mut RelaySink<LifecycleEvent>, ready: mpsc::Sender<PumpReady>) {
    // SAFETY: the box was created by `Box::into_raw` in the constructor
    // and reaches here exactly once without ever being installed into a
    // live window.
    drop(unsafe { Box::from_raw(sink_ptr) });
    let _ = ready.send(PumpReady::Failed);
}

#[cfg(test)]
#[path = "lifecycle_tests.rs"]
mod tests;
