#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_

#include <windows.h>

#include <memory>
#include <string>

#include "browser/window/tab_controller.h"
#include "include/cef_app.h"
#include "windows/shell_command_adapter.h"

namespace crayon::browser::cef_shell {

class WindowsWindowIcons final {
 public:
  explicit WindowsWindowIcons(HINSTANCE resource_module);
  ~WindowsWindowIcons();

  bool valid() const { return main_icon_ != nullptr && small_icon_ != nullptr; }
  void Apply(CefRefPtr<CefBrowser> browser) const;

 private:
  HICON main_icon_ = nullptr;
  HICON small_icon_ = nullptr;

  WindowsWindowIcons(const WindowsWindowIcons&) = delete;
  WindowsWindowIcons& operator=(const WindowsWindowIcons&) = delete;
};

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  BrowserApp(HINSTANCE resource_module, std::wstring product_name);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnContextInitialized() override;
  CefRefPtr<CefClient> GetDefaultClient() override;
  bool brand_icons_valid() const { return window_icons_->valid(); }

 private:
  const std::wstring product_name_;
  const std::shared_ptr<WindowsWindowIcons> window_icons_;
  CefRefPtr<window::TabController> tab_controller_;
  const std::shared_ptr<WindowsShellRuntime> shell_runtime_;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_
