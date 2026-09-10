#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_PRODUCT_HOST_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_ALLOY_PRODUCT_HOST_MAC_H_

// PLT-SHELL-24M1/M2: production owner of the macOS Alloy first window.
// Assembles the full product UI: tab strip, toolbar (navigation + omnibox +
// cast entry), and browser view. Uses the TabController's normalized
// WindowClient so every existing handler surface keeps working.

#include <functional>
#include <memory>
#include <string>

#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_view.h"
#include "include/views/cef_window_delegate.h"

namespace crayon::browser::cef_shell::macos {

class AlloyProductHostMac final {
 public:
  struct Dependencies {
    /// TabController's normalized WindowClient (all handler surfaces).
    CefRefPtr<CefClient> client;
    std::string initial_url;
    std::string title;
  };

  struct Callbacks final {
    /// Invoked once the Alloy window is destroyed (app quit path).
    std::function<void()> window_destroyed;
  };

  AlloyProductHostMac(Dependencies dependencies, Callbacks callbacks);
  ~AlloyProductHostMac();

  AlloyProductHostMac(const AlloyProductHostMac&) = delete;
  AlloyProductHostMac& operator=(const AlloyProductHostMac&) = delete;

  /// Creates the CefWindow and assembles the full product UI.
  bool Start();

  /// Starts the browser close; the window destruction callback reports the
  /// completion (idempotent).
  void Close();

  bool started() const noexcept;
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
