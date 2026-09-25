#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "browser/branding/about_browser.h"
#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/media_host/alloy_cast_controller.h"
#include "browser/media_host/cast_entry_surface.h"
#include "browser/media_host/media_host_adapter.h"
#include "browser/page_markdown/cef_page_markdown_preview.h"
#include "browser/permission/permission_store.h"
#include "browser/window/tab_controller.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "crayon/browser_product_strings/product_strings.h"
#include "include/cef_app.h"
#include "macos/agent_host_bridge_mac.h"
#include "macos/alloy_product_host_mac.h"
#include "macos/alloy_toolbar_mac.h"
#include "browser/window/alloy_bookmarks.h"
#include "macos/application_menu_mac.h"
#include "macos/content_host_adapter_mac.h"

namespace crayon::browser::cef_shell {

namespace macos {
class TrustedInputMonitor;
}

// AGT-12Cc2r: serve-thread → UI-thread marshaling state for the CAAP
// agent host. Defined in app.cc; BrowserApp owns it and must declare it
// before the agent-host bridge member so the gate outlives teardown.
struct AgentUiState;

// Owns the standalone Chrome-style chrome://settings window. TU-local to
// app.cc; needs friend access to park the browser ref on the app.
class SettingsWindowClient;

class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit BrowserApp(
      ::crayon::browser::localization::LocaleSnapshot locale_snapshot);
  ~BrowserApp() override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefResourceBundleHandler> GetResourceBundleHandler() override {
    return about_resources_;
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
  // AppKit menu routing for commands with an Alloy-window surface (tab
  // strip, omnibox, navigation). Unhandled commands fall back to the
  // caller's Chrome-command path (popup compatibility).
  bool ExecuteAppCommand(macos::ApplicationCommand command);
  // Single funnel for every exit trigger (AppKit menu terminate, product
  // window destroyed). Stops the background service chain first and only
  // then closes the remaining browsers, so the message loop quits from
  // TabController's last OnBeforeClose — never with CEF objects still
  // alive, which is the CefShutdown CHECK-crash shape.
  void RequestProductQuit(bool force_close_browsers);
  // UI-thread notification from SettingsWindowClient: the standalone
  // settings window delivered OnBeforeClose. Releases the client refs and
  // resumes a quit that was parked on this window.
  void OnSettingsBrowserClosed();

 private:
  void ContinueContentHostStartup();
  void ScheduleContentHostTick();
  void ContentHostTick();
  void ConsumeMediaObservations();
  void SyncToolbarToActiveTab();
  void BindCastForActiveTab();
  // PLT-SHELL-24M2FIX-C6: bookmark store behind the address bar control.
  // EnsureBookmarks creates and loads it on first use; RefreshBookmarkState
  // reflects the active tab on the control; ToggleActiveBookmark adds or
  // removes the active page and persists the store.
  // PLT-SHELL-24M2FIX-C11: settings are Chromium's own page. The product keeps
  // no settings surface of its own, so every entry point (the toolbar menu and
  // the application menu's Preferences item) loads chrome://settings into the
  // active tab. The product's new-tab page is the blank page.
  void OpenOriginalSettings();
  bool EnsureBookmarks();
  bool RefreshBookmarkState();
  void ToggleActiveBookmark();
  bool SaveBookmarks();
  // PLT-SHELL-24M2UIP-a: attaches the permanent cast entry as soon as the
  // first browser view exists, independent of media readiness.
  void TryAttachCastEntry();
  void DetachCastSurface();
  void ResetCastContext();
  // AGT-12Cc2r: agent-host callback plumbing. The state member is
  // declared before agent_host_ so it outlives the bridge teardown.
  void StartAgentHost();
  void ShutdownAgentHost();
  // Idempotent teardown of the service chain normally driven by
  // TabController's browsers-closed callback.
  void StopBackgroundServices();
  // UI-thread data readers backing the CAAP page tools.
  std::string AgentActiveTabIdForUi();
  bool AgentTabKnownForUi(const std::string& tab);
  std::string AgentExecuteToolForUi(const std::string& tool,
                                    const std::string& tab);
  // AGT-05C: presents the connect-level confirmation sheet; the allow
  // button mints the session grant for the connected client.
  void OnAgentClientConnectedForUi(const std::string& client,
                                   const std::string& capabilities);

  friend class SettingsWindowClient;

  const CefRefPtr<branding::AboutBrowserResources> about_resources_;
  const ::crayon::browser::localization::LocaleSnapshot locale_snapshot_;
  const ::crayon::browser::product_strings::ProductStrings product_strings_;
  const page_markdown::PageMarkdownStrings page_markdown_strings_;
  const std::shared_ptr<mdv::MdvRuntimeState> mdv_runtime_;
  const std::shared_ptr<mdv::MdvEntryController> mdv_entries_;
  const std::shared_ptr<mdv::MdvEditController> mdv_editing_;
  std::unique_ptr<permission::PermissionStore> permission_store_;
  std::unique_ptr<macos::ContentHostAdapter> content_host_;
  std::unique_ptr<media_host::MediaHostAdapter> media_host_;
  std::unique_ptr<media_host::AlloyCastController> cast_controller_;
  std::unique_ptr<CastEntrySurface> cast_surface_;
  std::map<std::uint32_t, std::uint32_t> media_generations_;
  std::optional<browser_cast_view::CastViewContext> cast_binding_attempt_;
  std::uint64_t cast_browser_session_ = 0;
  bool cast_context_bound_ = false;
  std::unique_ptr<macos::TrustedInputMonitor> trusted_input_monitor_;
  // C20a (roadmap §114): the product main window is CEF's own Chrome-style
  // window (CefBrowserHost::CreateBrowser with CHROME runtime style, via
  // TabController::CreateMainWindow). Chromium owns the tab strip, toolbar,
  // window controls, popups and chrome:// pages; the shell owns no chrome
  // surfaces. The cast action button mounts as a titlebar accessory
  // (CastChromeMac) in C20c.
  std::unique_ptr<macos::AlloyToolbarMac> toolbar_;
  /// PLT-SHELL-24M2FIX-C6: bookmark store behind the address bar's control.
  /// Created on first use, so a shell that never bookmarks never touches disk.
  std::unique_ptr<window::AlloyBookmarks> bookmarks_;
  // AGT-12Cc2r: serve-thread → UI-thread marshaling state for the agent
  // host. Declared before agent_host_ so the gate outlives bridge stop().
  std::unique_ptr<AgentUiState> agent_ui_state_;
  std::unique_ptr<macos::AgentHostBridgeMac> agent_host_;
  CefRefPtr<window::TabController> tab_controller_;
  std::unique_ptr<page_markdown::CefPageMarkdownPreviewController>
      page_markdown_preview_;
  std::size_t content_host_start_checks_ = 0;
  bool content_host_tick_active_ = false;
  bool background_services_stopped_ = false;
  // Standalone Chrome-style settings window (outside the tab model). The
  // client outlives the window until OnBeforeClose releases both refs; a
  // quit arriving while the window is open parks on settings_quit_pending_.
  CefRefPtr<SettingsWindowClient> settings_client_;
  CefRefPtr<CefBrowser> settings_browser_;
  bool settings_quit_pending_ = false;
  bool media_host_was_healthy_ = false;
  std::uint64_t media_host_cast_epoch_ = 0;
  int active_browser_id_ = 0;

  IMPLEMENT_REFCOUNTING(BrowserApp);
  DISALLOW_COPY_AND_ASSIGN(BrowserApp);
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_APP_H_
