#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_SETTINGS_PANEL_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_SETTINGS_PANEL_MAC_H_

// PLT-SHELL-24M2FIX-C9: the product's first settings surface.
//
// A standalone non-activating panel with one editable field, following
// agent_confirm_sheet_mac.{h,mm} for the same reason recorded there: the product
// runs CefRunMessageLoop rather than NSApp's run loop, and sheet/modal
// completion machinery does not survive that pump. One panel at a time; the
// callback runs on the UI thread after the panel is gone.
//
// The shell has no crayon://settings page yet, so this is where a preference
// becomes user-editable; when the built-in settings page lands, it should call
// the same preference path rather than growing a second owner.

#include <functional>
#include <string>

namespace crayon::browser::cef_shell::settings_panel {

struct PanelCallbacks {
  /// Invoked with the entered text when the user saves. Empty when cancelled.
  std::function<void(std::string)> save;
};

/// Presents the settings panel. All labels are resolved by the caller from the
/// locale catalog, so this layer owns presentation only. Returns false when a
/// panel is already presented (the request is dropped rather than queued).
bool PresentSettingsPanel(const std::string& title, const std::string& field_label,
                          const std::string& field_value,
                          const std::string& save_label,
                          const std::string& cancel_label,
                          const PanelCallbacks& callbacks);

/// Ends a presented panel without invoking save (shutdown funnel).
void DismissSettingsPanel();

}  // namespace crayon::browser::cef_shell::settings_panel

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_SETTINGS_PANEL_MAC_H_
