#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_CONFIRM_SHEET_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_CONFIRM_SHEET_MAC_H_

// AGT-05C: product presentation for the connect-level capability
// confirmation. A standalone non-activating panel window (deliberately
// not an NSAlert sheet — sheet completion machinery does not survive the
// product's CefRunMessageLoop pump); the allowed callback runs on the
// UI thread after the user confirms.

#include <functional>
#include <string>

namespace crayon::browser::cef_shell::agent_confirm {

struct SheetCallbacks {
  /// Invoked on the UI thread when the user allows; empty when denied.
  std::function<void()> allowed;
};

/// Presents the connect confirmation panel. Button labels come from the
/// locale catalog at the call site. Returns false when a panel is
/// already presented — the connect event is then dropped and the client
/// stays ungranted (default deny). The allow callback must carry the
/// confirmed client name; the host refuses the mint once a different
/// connection is active (AGT-05C client binding).
bool PresentConnectConfirmPanel(const std::string& title,
                                const std::string& detail,
                                const std::string& allow_label,
                                const std::string& deny_label,
                                const SheetCallbacks& callbacks);

/// Ends a presented panel (deny path) without invoking the allow
/// callback. UI thread only; used by the shutdown funnel so an open
/// confirmation can never block app termination.
void DismissConnectConfirmPanel();

}  // namespace crayon::browser::cef_shell::agent_confirm

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_CONFIRM_SHEET_MAC_H_
