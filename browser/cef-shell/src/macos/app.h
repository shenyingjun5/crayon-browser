#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_

#include <string>

#include "browser/window/tab_controller.h"
#include "include/cef_app.h"

namespace crayon::browser::cef_shell {

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(std::string product_name);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnContextInitialized() override;
  // Chrome-style windows created by the Chrome UI (new tab/window, popups)
  // must run through our client so callbacks stay normalized.
  CefRefPtr<CefClient> GetDefaultClient() override;
  CefRefPtr<window::TabController> tab_controller() const {
    return tab_controller_;
  }

 private:
  const std::string product_name_;
  CefRefPtr<window::TabController> tab_controller_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
