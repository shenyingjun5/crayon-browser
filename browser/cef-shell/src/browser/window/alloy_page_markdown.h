#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "browser/page_markdown/cef_page_markdown_preview.h"
#include "browser/page_snapshot_gateway/cef_page_snapshot_bridge.h"
#include "browser/window/alloy_builtin_content.h"
#include "browser/window/alloy_tab_controller.h"

namespace crayon::browser::cef_shell::window {

// Candidate-host adapter for the existing Browser-issued snapshot -> Core ->
// Markdown preview pipeline. It owns only the CEF bridge/controller wiring;
// tab facts, Markdown state and content-host processing retain their owners.
class AlloyPageMarkdown final : public AlloyBuiltinContentObserver {
 public:
  AlloyPageMarkdown(
      AlloyTabController* tabs,
      std::shared_ptr<mdv::MdvEditController> mdv_editing,
      page_markdown::PageMarkdownStrings strings,
      std::function<bool(const std::string&)> clipboard_write,
      AlloyBuiltinContentObserver* downstream = nullptr);
  ~AlloyPageMarkdown() override;

  void SetSnapshotObserver(gateway::PageSnapshotObserver* observer);
  void SetSnapshotAdmission(std::function<bool()> admission);
  void SetEventsReadyCallback(std::function<void()> callback);
  std::vector<gateway::SnapshotGatewayEvent> DrainSnapshots(
      std::size_t max_events);
  void Tick(std::vector<::crayon::cef_shell::ipc::content_host::Message> replies,
            bool content_host_healthy);
  void Shutdown();

  void OnBuiltinBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        int http_status_code) override;
  void OnBuiltinLoadError(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          cef_errorcode_t error_code,
                          const CefString& error_text,
                          const CefString& failed_url) override;
  void OnBuiltinTitleChange(CefRefPtr<CefBrowser> browser,
                            const CefString& title) override;
  void OnBuiltinAddressChange(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              const CefString& url) override;
  void OnBuiltinLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                   bool is_loading, bool can_go_back,
                                   bool can_go_forward) override;
  bool OnBuiltinConsoleMessage(CefRefPtr<CefBrowser> browser,
                               cef_log_severity_t level,
                               const CefString& message,
                               const CefString& source, int line) override;
  void OnBuiltinBrowserClosing(CefRefPtr<CefBrowser> browser) override;
  void OnBuiltinRenderProcessTerminated(
      CefRefPtr<CefBrowser> browser) override;
  bool OnBuiltinProcessMessageReceived(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefProcessId source_process,
      CefRefPtr<CefProcessMessage> message) override;
  void OnBuiltinBeforeContextMenu(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefContextMenuParams> params,
      CefRefPtr<CefMenuModel> model) override;
  bool OnBuiltinContextMenuCommand(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefContextMenuParams> params, int command_id,
      cef_event_flags_t event_flags) override;

 private:
  std::optional<page_markdown::PageMarkdownTabState> Lookup(int browser_id) const;
  std::optional<browser_engine::SnapshotRequestId> Start(
      CefRefPtr<CefBrowser> browser);
  void Cancel(const browser_engine::SnapshotRequestId& request_id);
  void NotifyEventsReady();

  AlloyTabController* tabs_;
  AlloyBuiltinContentObserver* downstream_;
  gateway::CefPageSnapshotBridge bridge_;
  page_markdown::CefPageMarkdownPreviewController preview_;
  std::function<bool()> admission_;
  std::function<void()> events_ready_;
  bool active_ = true;
};

}  // namespace crayon::browser::cef_shell::window
