#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TOOLBAR_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TOOLBAR_MAC_H_

// PLT-SHELL-24M2: macOS product toolbar assembly. Owns the shared window
// components (tab strip, navigation, omnibox) and the horizontal toolbar
// panel; forwards tab intents to the app. Owns no tab state.

#include <functional>
#include <memory>
#include <string>

#include "browser/window/alloy_navigation.h"
#include "browser/window/alloy_omnibox.h"
#include "browser/window/alloy_tab_strip.h"
#include "browser/window/tab_model.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/cef_browser.h"
#include "include/views/cef_panel.h"

namespace crayon::browser::cef_shell::macos {

class AlloyToolbarMac final {
 public:
  struct Callbacks final {
    std::function<void()> new_tab;
    std::function<void(window::TabId)> activate_tab;
    std::function<void(window::TabId)> close_tab;
    std::function<std::string(window::TabId)> tab_title;
  };

  AlloyToolbarMac(localization::LocaleSnapshot locale, Callbacks callbacks);
  ~AlloyToolbarMac();

  AlloyToolbarMac(const AlloyToolbarMac&) = delete;
  AlloyToolbarMac& operator=(const AlloyToolbarMac&) = delete;

  CefRefPtr<CefView> tab_strip_view() const;
  CefRefPtr<CefView> toolbar_view() const;

  bool SyncTabs(const window::TabModel& model);

  /// Binds the navigation projection to the active tab's browser.
  bool AttachBrowser(window::TabId tab_id, CefRefPtr<CefBrowser> browser);

  /// Projects one TabController UI update (address/loading) into the
  /// navigation and omnibox views. browser_id 0 = model reshuffle only.
  bool OnTabUiUpdate(int browser_id, const std::string& url, bool is_loading,
                     bool can_go_back, bool can_go_forward);

  bool SetAddress(std::string address);
  bool FocusOmnibox();
  void Shutdown();

 private:
  std::unique_ptr<window::AlloyTabStrip> tab_strip_;
  std::unique_ptr<window::AlloyOmnibox> omnibox_;
  std::unique_ptr<window::AlloyNavigation> navigation_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefBrowser> bound_browser_;
};

}  // namespace crayon::browser::cef_shell::macos

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TOOLBAR_MAC_H_
