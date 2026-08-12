#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_

#include <string>
#include <vector>

#include <windows.h>

#include "include/cef_app.h"
#include "include/cef_client.h"

namespace crayon::browser::cef_shell {

class BrowserClient final : public CefClient, public CefLifeSpanHandler {
public:
  explicit BrowserClient(HINSTANCE resource_module);
  ~BrowserClient() override;

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool brand_icons_valid() const {
    return main_icon_ != nullptr && small_icon_ != nullptr;
  }

private:
  std::vector<CefRefPtr<CefBrowser>> browsers_;
  HICON main_icon_ = nullptr;
  HICON small_icon_ = nullptr;

  IMPLEMENT_REFCOUNTING(BrowserClient);
  DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
public:
  BrowserApp(HINSTANCE resource_module, std::wstring product_name);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnContextInitialized() override;
  bool brand_icons_valid() const { return client_->brand_icons_valid(); }

private:
  const std::wstring product_name_;
  CefRefPtr<BrowserClient> client_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

} // namespace crayon::browser::cef_shell

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_
