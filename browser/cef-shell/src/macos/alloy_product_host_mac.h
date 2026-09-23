#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_PRODUCT_HOST_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_PRODUCT_HOST_MAC_H_

// PLT-SHELL-24M1/M2: production owner of the macOS Alloy first window.
// Assembles the product layout: tab strip, toolbar, and the content
// container that hosts one browser view per tab. Uses the TabController's
// normalized WindowClient so every existing handler surface keeps working.
// The host owns window/layout/lifecycle only — no business logic.

#include <functional>
#include <memory>
#include <string>

#include "browser/window/alloy_chrome_decoration.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"

namespace crayon::browser::cef_shell::macos {

class AlloyProductHostMac final {
 public:
  struct Dependencies {
    /// TabController's normalized WindowClient (all handler surfaces).
    CefRefPtr<CefClient> client;
    std::string initial_url;
    std::string title;
    /// Optional Alloy chrome views (owned by the app's toolbar assembly);
    /// inserted above the content container when present.
    CefRefPtr<CefView> tab_strip_view;
    CefRefPtr<CefView> toolbar_view;
    /// PLT-SHELL-24M2FIX-C4: chrome geometry the native decoration has to draw
    /// (tab corners, tab loading indicator, omnibox pill). The supplier owns
    /// the views and therefore the geometry; the host only owns when to
    /// republish it (window creation, every layout pass). Unset means no
    /// decoration is drawn.
    std::function<window::ChromeDecoration()> chrome_decoration;
  };

  struct Callbacks final {
    /// Invoked once the Alloy window is destroyed (app quit path).
    std::function<void()> window_destroyed;
    std::function<void()> view_ready;
    std::function<void()> before_close;
    std::function<void()> layout_changed;
    std::function<bool(const CefKeyEvent&)> key_event;
    std::function<bool(int)> accelerator;
  };

  AlloyProductHostMac(Dependencies dependencies, Callbacks callbacks);
  ~AlloyProductHostMac();

  AlloyProductHostMac(const AlloyProductHostMac&) = delete;
  AlloyProductHostMac& operator=(const AlloyProductHostMac&) = delete;

  /// Creates the CefWindow and assembles the product UI.
  bool Start();

  /// Starts the browser close; the window destruction callback reports the
  /// completion (idempotent).
  void Close();

  /// Consume DoClose only for an owned view; release it after the callback.
  bool HandleBrowserClose(CefRefPtr<CefBrowser> browser);
  /// OnBeforeClose is the authoritative close completion, not view destruction.
  void NotifyBrowserClosed(int browser_id);

  /// Creates one more tab hosting |url| in the content container. The model
  /// binding and activation happen through the shared WindowClient callbacks.
  bool CreateTab(const std::string& url);

  /// Makes the view hosting |browser_id| the visible tab.
  void ShowBrowser(int browser_id);

  /// Recompute tab hit regions after the toolbar rebuilds its CEF tab rows.
  void RefreshTabChrome();

  /// PLT-SHELL-24M2FIX-D: emits the product's real view tree, container
  /// children, active browser id and per-browser main-frame URLs on stderr
  /// when CRAYON_SHELL_DIAG is set; inert otherwise. Needed because a harness
  /// probe that assembles the same host class still cannot see the app's tab
  /// lifecycle, so it cannot falsify an on-screen blank content area.
  void DumpDiagnostics(const char* when);

  CefRefPtr<CefWindow> window() const;
  CefRefPtr<CefBrowserView> browser_view(int browser_id) const;
  bool started() const noexcept;
  /// The browser of the currently visible tab.
  CefRefPtr<CefBrowser> browser() const noexcept;
  const std::string& initial_url() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  CefRefPtr<CefBrowserViewDelegate> view_delegate_;
  CefRefPtr<CefWindowDelegate> window_delegate_;
};

}  // namespace crayon::browser::cef_shell::macos

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_PRODUCT_HOST_MAC_H_
