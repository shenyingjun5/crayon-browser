#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_

#include <memory>
#include <string>

#include "browser/permission/permission_store.h"
#include "browser/window/tab_controller.h"

#include "include/cef_app.h"

namespace crayon::browser::cef_shell {

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(std::string product_name);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  // Dev/ad-hoc builds cannot anchor a Keychain ACL (every rebuild
  // changes the signature), so Chromium's "Safe Storage" key would
  // prompt for keychain access on every launch.  Real keychain-backed
  // secret storage lands with the SecureStore platform adapter
  // (PLT-M04/PRV-05) behind a stable signing identity; until then the
  // in-memory mock keeps launches prompt-free.
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  void OnContextInitialized() override;
  // Chrome-style windows created by the Chrome UI (new tab/window, popups)
  // must run through our client so callbacks stay normalized.
  CefRefPtr<CefClient> GetDefaultClient() override;
  CefRefPtr<window::TabController> tab_controller() const {
    return tab_controller_;
  }

 private:
  const std::string product_name_;
  std::unique_ptr<permission::PermissionStore> permission_store_;
  CefRefPtr<window::TabController> tab_controller_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
