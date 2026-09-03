#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_

#include <cstdint>
#include <memory>
#include <string>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/media_host/media_host_adapter.h"
#include "browser/page_markdown/cef_page_markdown_preview.h"
#include "browser/permission/permission_store.h"
#include "browser/window/tab_controller.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "crayon/browser_product_strings/product_strings.h"
#include "include/cef_app.h"
#include "macos/cast_chrome_mac.h"
#include "browser/media_host/cast_shell_controller.h"
#include "macos/content_host_adapter_mac.h"

namespace crayon::browser::cef_shell {

namespace macos {
class TrustedInputMonitor;
}

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(
      ::crayon::browser::localization::LocaleSnapshot locale_snapshot);
  ~BrowserApp() override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  // Product decision (2026-08-23): this browser never stores its
  // cookie-encryption "Safe Storage" key in the system keychain — dev
  // or release, ad-hoc or signed.  The macOS keychain is touched only
  // by the SecureStore platform adapter (PLT-M04/PRV-05) when the user
  // actually saves or reads a secret, so launches stay prompt-free by
  // design and cookies at rest use the in-memory mock key.
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
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
  bool product_strings_valid() const;

 private:
  void ContinueContentHostStartup();
  void ScheduleContentHostTick();
  void ContentHostTick();
  void ConsumeMediaObservations();

  const ::crayon::browser::product_strings::ProductStrings product_strings_;
  const page_markdown::PageMarkdownStrings page_markdown_strings_;
  const macos::CastChromeStrings cast_strings_;
  const std::shared_ptr<mdv::MdvRuntimeState> mdv_runtime_;
  const std::shared_ptr<mdv::MdvEntryController> mdv_entries_;
  const std::shared_ptr<mdv::MdvEditController> mdv_editing_;
  std::unique_ptr<permission::PermissionStore> permission_store_;
  std::unique_ptr<macos::ContentHostAdapter> content_host_;
  std::unique_ptr<media_host::MediaHostAdapter> media_host_;
  std::unique_ptr<media_host::CastShellController> cast_shell_;
  std::unique_ptr<macos::CastChromeMac> cast_chrome_;
  std::unique_ptr<macos::TrustedInputMonitor> trusted_input_monitor_;
  CefRefPtr<window::TabController> tab_controller_;
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

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
