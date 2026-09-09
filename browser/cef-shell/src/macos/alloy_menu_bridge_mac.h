#pragma once

#include <functional>

#include "include/cef_browser.h"
#include "macos/application_menu_mac.h"

namespace crayon::browser::cef_shell::mdv {
class MdvEntryController;
}

namespace crayon::browser::cef_shell::macos {

/// Maps the closed `ApplicationCommand` set onto the Alloy owners for the
/// macOS candidate host (PLT-SHELL-17M).
///
/// Command-target constraint: every command acts only on the current Alloy
/// browser supplied by `active_browser`; a command without a wired owner is
/// a bounded no-op that reports false. "Open file" routes exclusively
/// through the `MdvEntryController` runtime-neutral seam (controlled
/// single-`.md` dialog); clipboard commands operate on the current main
/// frame only and never carry paths or credentials. Raw local paths never
/// enter logs.
class AlloyMenuBridgeMac final {
 public:
  struct Owners {
    /// Current Alloy browser provider; null/absent browser rejects commands.
    std::function<CefRefPtr<CefBrowser>()> active_browser;
    /// Runtime-neutral open-file seam (MdvEntryController).
    std::function<bool(CefRefPtr<CefBrowser>)> open_markdown;
    /// Branding destinations for About; licenses navigate to the fixed
    /// `chrome://credits/` target.
    std::function<bool(CefRefPtr<CefBrowser>, const std::string&)> navigate;
    /// Hooks for owners wired by later migration tasks (18M..23M); an
    /// absent hook keeps the command a bounded no-op.
    std::function<bool(CefRefPtr<CefBrowser>)> save;
    std::function<bool(CefRefPtr<CefBrowser>)> print;
    std::function<bool(CefRefPtr<CefBrowser>)> find;
    std::function<bool(CefRefPtr<CefBrowser>)> reload;
    std::function<bool(CefRefPtr<CefBrowser>)> back;
    std::function<bool(CefRefPtr<CefBrowser>)> forward;
    std::function<bool(CefRefPtr<CefBrowser>)> zoom_in;
    std::function<bool(CefRefPtr<CefBrowser>)> zoom_out;
    std::function<bool(CefRefPtr<CefBrowser>)> zoom_reset;
    std::function<bool()> new_tab;
    std::function<bool()> new_window;
    std::function<bool()> new_incognito_window;
    std::function<bool()> close_tab;
    std::function<bool()> focus_location;
    std::function<bool()> settings;
    std::function<bool()> next_tab;
    std::function<bool()> previous_tab;
  };

  explicit AlloyMenuBridgeMac(Owners owners);

  /// Dispatches one closed menu command. Returns false for unknown
  /// commands, a missing current browser, or an unwired owner.
  bool Execute(ApplicationCommand command);

 private:
  Owners owners_;
};

}  // namespace crayon::browser::cef_shell::macos
