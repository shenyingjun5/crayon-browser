#pragma once

#include "include/views/cef_browser_view.h"
#include "include/views/cef_panel.h"

namespace crayon::browser::cef_shell::window {

// A Views layout, not a navigation or casting owner. All calls, including
// destruction, run on the CEF UI thread. Detach before destroying the Browser.
class ChromeLocationBar final {
public:
  ChromeLocationBar() = default;
  ~ChromeLocationBar();
  ChromeLocationBar(const ChromeLocationBar &) = delete;
  ChromeLocationBar &operator=(const ChromeLocationBar &) = delete;

  // BrowserView must already belong to the same Window as parent. The action
  // is supplied/localized by its owner and must not already have a parent.
  bool Attach(CefRefPtr<CefPanel> parent,
              CefRefPtr<CefBrowserView> browser_view,
              CefRefPtr<CefView> trailing_action);

  // Release the borrowed location before a possibly synchronous close. When
  // TryCloseBrowser returns false, restore it while the Browser still lives.
  void SuspendLocation();
  bool RestoreLocation();
  void Detach();

  bool location_attached() const;
  CefRect LocationBoundsInScreen() const;
  CefRect ActionBoundsInScreen() const;

private:
  CefRefPtr<CefPanel> parent_;
  CefRefPtr<CefBrowserView> browser_view_;
  CefRefPtr<CefPanel> row_;
  CefRefPtr<CefView> location_;
  CefRefPtr<CefView> action_;
};

} // namespace crayon::browser::cef_shell::window
