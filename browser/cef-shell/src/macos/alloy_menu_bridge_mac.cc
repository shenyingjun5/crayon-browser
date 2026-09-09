#include "macos/alloy_menu_bridge_mac.h"

#include <string>

#include "browser/branding/about_destination.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::macos {
AlloyMenuBridgeMac::AlloyMenuBridgeMac(Owners owners)
    : owners_(std::move(owners)) {}

bool AlloyMenuBridgeMac::Execute(ApplicationCommand command) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefBrowser> browser =
      owners_.active_browser ? owners_.active_browser() : nullptr;
  if (!browser || !browser->GetMainFrame()) {
    return false;
  }
  const auto navigate = [&owners = owners_, &browser](const std::string& url) {
    return owners.navigate && owners.navigate(browser, url);
  };
  const auto optional =
      [&browser](const std::function<bool(CefRefPtr<CefBrowser>)>& hook) {
        return hook ? hook(browser) : false;
      };
  const auto optional_window =
      [](const std::function<bool()>& hook) { return hook ? hook() : false; };
  switch (command) {
    case ApplicationCommand::kOpenFile:
      // The only local-file entry: the controlled single-`.md` dialog
      // owned by the MDV entry controller (MDV-01 §3 E1).
      return owners_.open_markdown && owners_.open_markdown(browser);
    case ApplicationCommand::kAbout:
      return navigate(branding::kAboutBrowserUrl);
    case ApplicationCommand::kSave:
      return optional(owners_.save);
    case ApplicationCommand::kPrint:
      return optional(owners_.print);
    case ApplicationCommand::kFind:
      return optional(owners_.find);
    case ApplicationCommand::kReload:
      return optional(owners_.reload);
    case ApplicationCommand::kBack:
      return optional(owners_.back);
    case ApplicationCommand::kForward:
      return optional(owners_.forward);
    case ApplicationCommand::kZoomIn:
      return optional(owners_.zoom_in);
    case ApplicationCommand::kZoomOut:
      return optional(owners_.zoom_out);
    case ApplicationCommand::kZoomReset:
      return optional(owners_.zoom_reset);
    case ApplicationCommand::kNewTab:
      return optional_window(owners_.new_tab);
    case ApplicationCommand::kNewWindow:
      return optional_window(owners_.new_window);
    case ApplicationCommand::kNewIncognitoWindow:
      return optional_window(owners_.new_incognito_window);
    case ApplicationCommand::kCloseTab:
      return optional_window(owners_.close_tab);
    case ApplicationCommand::kFocusLocation:
      return optional_window(owners_.focus_location);
    case ApplicationCommand::kSettings:
      return optional_window(owners_.settings);
    case ApplicationCommand::kNextTab:
      return optional_window(owners_.next_tab);
    case ApplicationCommand::kPreviousTab:
      return optional_window(owners_.previous_tab);
  }
  return false;
}

}  // namespace crayon::browser::cef_shell::macos
