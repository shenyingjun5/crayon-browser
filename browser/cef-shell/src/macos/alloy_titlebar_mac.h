#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TITLEBAR_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TITLEBAR_MAC_H_

// PLT-SHELL-24M2UIP-c: Chrome-style merged titlebar for the Alloy product
// window — the tab strip row extends under a transparent titlebar and the
// traffic lights sit inside the strip's leading inset.

namespace crayon::browser::cef_shell::macos::titlebar {

/// Styles |native_window| (an NSWindow*) with a full-size content view,
/// transparent titlebar and hidden title, and places the traffic-light
/// buttons vertically centered in a |strip_height| tab strip row. Returns
/// false when the handle is not an NSWindow. Idempotent.
bool ApplyMergedTitlebar(void* native_window, double strip_height);

}  // namespace crayon::browser::cef_shell::macos::titlebar

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TITLEBAR_MAC_H_
