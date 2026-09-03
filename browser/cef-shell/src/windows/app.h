#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "browser/branding/about_browser.h"
#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/media_host/cast_shell_controller.h"
#include "browser/media_host/media_host_adapter.h"
#include "browser/page_markdown/cef_page_markdown_preview.h"
#include "browser/permission/permission_store.h"
#include "browser/window/tab_controller.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "crayon/browser_new_tab/new_tab_page.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "crayon/browser_product_strings/product_strings.h"
#include "include/cef_app.h"
#include "windows/content_host_adapter_win.h"
#include "windows/cast_chrome_win.h"
#include "windows/media_host_process_win.h"
#include "windows/shell_command_adapter.h"
#include "windows/trusted_input_monitor_win.h"

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

  WindowsWindowIcons(const WindowsWindowIcons &) = delete;
  WindowsWindowIcons &operator=(const WindowsWindowIcons &) = delete;
};

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  BrowserApp(HINSTANCE resource_module,
             ::crayon::browser::localization::LocaleSnapshot locale_snapshot);
  ~BrowserApp() override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefResourceBundleHandler> GetResourceBundleHandler() override {
    return about_resources_;
  }
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnContextInitialized() override;
  CefRefPtr<CefClient> GetDefaultClient() override;
  bool brand_icons_valid() const { return window_icons_->valid(); }
  bool new_tab_strings_valid() const;
  bool mdv_strings_valid() const;
  bool page_markdown_strings_valid() const;
  bool cast_strings_valid() const;

 private:
  void ContinueContentHostStartup();
  void ScheduleContentHostTick();
  void ContentHostTick();
  void ConsumeMediaObservations();

  const std::shared_ptr<WindowsWindowIcons> window_icons_;
  const CefRefPtr<branding::AboutBrowserResources> about_resources_;
  const ::crayon::browser::product_strings::ProductStrings product_strings_;
  const page_markdown::PageMarkdownStrings page_markdown_strings_;
  const windows::CastChromeStrings cast_strings_;
  const std::shared_ptr<mdv::MdvRuntimeState> mdv_runtime_;
  const std::shared_ptr<mdv::MdvEntryController> mdv_entries_;
  const std::shared_ptr<mdv::MdvEditController> mdv_editing_;
  std::unique_ptr<permission::PermissionStore> permission_store_;
  std::unique_ptr<windows::ContentHostAdapter> content_host_;
  std::unique_ptr<media_host::MediaHostAdapter> media_host_;
  std::unique_ptr<media_host::CastShellController> cast_shell_;
  std::unique_ptr<windows::CastChromeWin> cast_chrome_;
  std::unique_ptr<windows::TrustedInputMonitorWin> trusted_input_monitor_;
  CefRefPtr<window::TabController> tab_controller_;
  const std::shared_ptr<WindowsShellRuntime> shell_runtime_;
  std::unique_ptr<page_markdown::CefPageMarkdownPreviewController>
      page_markdown_preview_;
  std::size_t content_host_start_checks_ = 0;
  bool content_host_tick_active_ = false;
  bool media_host_was_healthy_ = false;
  std::uint64_t media_host_cast_epoch_ = 0;
  int active_browser_id_ = 0;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_APP_H_
