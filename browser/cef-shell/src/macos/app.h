#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_

#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_client.h"

namespace crayon::browser::cef_shell {

class BrowserClient final : public CefClient, public CefLifeSpanHandler {
 public:
  BrowserClient() = default;

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void CloseAllBrowsers(bool force_close);
  void ShowMainWindow();

 private:
  std::vector<CefRefPtr<CefBrowser>> browsers_;
  bool is_closing_ = false;

  IMPLEMENT_REFCOUNTING(BrowserClient);
  DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(std::string product_name);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnContextInitialized() override;
  CefRefPtr<BrowserClient> browser_client() const { return browser_client_; }

 private:
  const std::string product_name_;
  CefRefPtr<BrowserClient> browser_client_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
