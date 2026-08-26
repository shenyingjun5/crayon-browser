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

  // Product decision (2026-08-23): this browser never stores its
  // cookie-encryption "Safe Storage" key in the system keychain — dev
  // or release, ad-hoc or signed.  The macOS keychain is touched only
  // by the SecureStore platform adapter (PLT-M04/PRV-05) when the user
  // actually saves or reads a secret, so launches stay prompt-free by
  // design and cookies at rest use the in-memory mock key.
  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;
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
