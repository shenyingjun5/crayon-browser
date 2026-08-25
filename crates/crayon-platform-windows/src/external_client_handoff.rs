//! Standalone cast-client handoff over the Windows shell (PLT-W04d).
//!
//! The adapter performs exactly the confirmed request: launching the
//! installed client or opening the official download page in the default
//! browser.  It never claims a casting session started (the closed
//! outcome set has no such variant) and carries no page data.  The
//! launch path and download URL are injected by the product assembly —
//! no machine-specific paths or URLs live in source.

use crayon_platform_api::external_client_handoff::{
    ExternalClientHandoff, HandoffAction, HandoffError, HandoffOutcome, HandoffRequest,
};
use std::path::PathBuf;

/// Shell open command for default-application handoff.
const VERB_OPEN: &[u16] = &[b'o' as u16, b'p' as u16, b'e' as u16, b'n' as u16, 0];
/// `SW_SHOWNORMAL`.
const SW_SHOWNORMAL: i32 = 1;

/// What the executor was asked to open.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum LaunchTarget {
    /// An executable path on this machine.
    Executable(PathBuf),
    /// An https URL handed to the default browser.
    Url(String),
}

/// Result of an OS shell launch attempt.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExecuteFailure {
    /// The target does not exist.
    NotFound,
    /// The shell refused the launch.
    Refused,
}

/// Injected shell operation; the production wiring calls
/// `ShellExecuteW`, tests record invocations.
pub type ShellOpen = Box<dyn FnMut(&LaunchTarget) -> Result<(), ExecuteFailure> + Send>;

/// Windows implementation of the external client handoff contract.
pub struct WindowsClientHandoff {
    installed_client: PathBuf,
    download_page: String,
    shell_open: ShellOpen,
}

impl std::fmt::Debug for WindowsClientHandoff {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("WindowsClientHandoff")
            .field("installed_client", &self.installed_client)
            .field("download_page", &self.download_page)
            .finish_non_exhaustive()
    }
}

impl WindowsClientHandoff {
    /// Creates the handoff with the real shell executor.
    ///
    /// `download_url` must be https; the launch path is only consulted
    /// for existence at perform time.
    pub fn new(installed_client: PathBuf, download_url: &str) -> Result<Self, HandoffError> {
        if !download_url.starts_with("https://") {
            return Err(HandoffError::Unavailable);
        }
        Ok(Self {
            installed_client,
            download_page: download_url.to_owned(),
            shell_open: Box::new(shell_execute),
        })
    }

    /// Creates the handoff with an injected executor (hermetic tests and
    /// alternative product assemblies).
    #[must_use]
    pub fn with_executor(
        installed_client: PathBuf,
        download_url: String,
        shell_open: ShellOpen,
    ) -> Self {
        Self {
            installed_client,
            download_page: download_url,
            shell_open,
        }
    }
}

impl ExternalClientHandoff for WindowsClientHandoff {
    fn perform(&mut self, request: &HandoffRequest) -> Result<HandoffOutcome, HandoffError> {
        let target = match request.action() {
            HandoffAction::LaunchClient => {
                if !self.installed_client.is_file() {
                    return Ok(HandoffOutcome::NotInstalled);
                }
                LaunchTarget::Executable(self.installed_client.clone())
            }
            HandoffAction::DownloadClient => LaunchTarget::Url(self.download_page.clone()),
        };
        match (self.shell_open)(&target) {
            Ok(()) => match request.action() {
                HandoffAction::LaunchClient => Ok(HandoffOutcome::LaunchRequested),
                HandoffAction::DownloadClient => Ok(HandoffOutcome::DownloadStarted),
            },
            Err(ExecuteFailure::NotFound) => Ok(HandoffOutcome::NotInstalled),
            Err(ExecuteFailure::Refused) => Err(HandoffError::Unavailable),
        }
    }
}

/// Real executor: `ShellExecuteW` with the default "open" verb.
fn shell_execute(target: &LaunchTarget) -> Result<(), ExecuteFailure> {
    let wide = |text: &str| -> Vec<u16> { text.encode_utf16().chain(std::iter::once(0)).collect() };
    let (file_wide, parameters): (Vec<u16>, Vec<u16>) = match target {
        LaunchTarget::Executable(path) => (wide(&path.to_string_lossy()), vec![0]),
        LaunchTarget::Url(url) => (wide(url), vec![0]),
    };
    // SAFETY: all string arguments are NUL-terminated UTF-16 for the call;
    // the returned instance handle is interpreted per the documented
    // value > 32 success rule (cast keeps pointer provenance opaque).
    let result = unsafe {
        windows_sys::Win32::UI::Shell::ShellExecuteW(
            std::ptr::null_mut(),
            VERB_OPEN.as_ptr(),
            file_wide.as_ptr(),
            parameters.as_ptr(),
            std::ptr::null(),
            SW_SHOWNORMAL,
        )
    };
    let code = result as isize;
    if code <= 32 {
        // Values 2/3 map to "file not found" per the documented table.
        if code == 2 || code == 3 {
            Err(ExecuteFailure::NotFound)
        } else {
            Err(ExecuteFailure::Refused)
        }
    } else {
        Ok(())
    }
}

#[cfg(test)]
#[path = "external_client_handoff_tests.rs"]
mod tests;
