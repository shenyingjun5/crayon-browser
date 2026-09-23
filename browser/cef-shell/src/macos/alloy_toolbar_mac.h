#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TOOLBAR_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TOOLBAR_MAC_H_

// PLT-SHELL-24M2: macOS product toolbar assembly. Owns the shared window
// components (tab strip, navigation, omnibox) and the horizontal toolbar
// panel; forwards tab intents to the app. Owns no tab state.

#include <functional>
#include <memory>
#include <string>

#include "browser/window/alloy_chrome_decoration.h"
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
  CefRefPtr<CefPanel> toolbar_panel() const { return toolbar_; }
  /// The omnibox field itself. Consumers must ask for it by name: the pill is
  /// wrapped in a holder panel, so the toolbar's child index no longer
  /// identifies it.
  CefRefPtr<CefView> omnibox_view() const;

  bool SyncTabs(const window::TabModel& model);

  /// Binds the navigation projection to the active tab's browser.
  bool AttachBrowser(window::TabId tab_id, CefRefPtr<CefBrowser> browser);

  /// Projects one TabController UI update (address/loading) into the
  /// navigation and omnibox views. browser_id 0 = model reshuffle only.
  bool OnTabUiUpdate(int browser_id, const std::string& url, bool is_loading,
                     bool can_go_back, bool can_go_forward);

  /// PLT-SHELL-24M2FIX-B: projects a failed main-frame navigation for
  /// |browser_id|. The address bar leaves its loading presentation and shows
  /// the failed URL, and the site identity reports the failure, so a dead
  /// navigation is no longer indistinguishable from an idle blank page.
  /// / |certificate_error| selects the security-specific identity.
  bool OnTabLoadError(int browser_id, const std::string& url,
                      bool certificate_error);

  bool SetAddress(std::string address);
  bool FocusOmnibox();

  /// PLT-SHELL-24M2FIX-C4: everything the native chrome decoration has to draw
  /// that CEF Views cannot express — tab corners and loading indicator geometry
  /// from the strip, plus the omnibox pill rect. The omnibox contributes here
  /// because this class is the assembly that owns both rows; the tab strip
  /// alone cannot see the navigation bar.
  window::ChromeDecoration decoration() const;

  void Shutdown();

 private:
  std::unique_ptr<window::AlloyTabStrip> tab_strip_;
  std::unique_ptr<window::AlloyOmnibox> omnibox_;
  std::unique_ptr<window::AlloyNavigation> navigation_;
  CefRefPtr<CefPanel> toolbar_;
  /// Pill container. The toolbar is one row tall and its children are stretched
  /// to fill it, so without this padded holder the omnibox panel would be as
  /// tall as the navigation bar and the pill would lose its shape.
  CefRefPtr<CefPanel> omnibox_holder_;
  CefRefPtr<CefBrowser> bound_browser_;
};

}  // namespace crayon::browser::cef_shell::macos

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_TOOLBAR_MAC_H_
