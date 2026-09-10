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
  };

  struct Callbacks final {
    /// Invoked once the Alloy window is destroyed (app quit path).
    std::function<void()> window_destroyed;
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

  /// Creates one more tab hosting |url| in the content container. The model
  /// binding and activation happen through the shared WindowClient callbacks.
  bool CreateTab(const std::string& url);

  /// Makes the view hosting |browser_id| the visible tab.
  void ShowBrowser(int browser_id);

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
