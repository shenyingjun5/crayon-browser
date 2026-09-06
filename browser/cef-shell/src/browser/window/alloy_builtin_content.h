#pragma once

#include <functional>
#include <memory>
#include <string>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "crayon/browser_new_tab/new_tab_page.h"
#include "include/cef_client.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_keyboard_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_message_router.h"

namespace crayon::browser::cef_shell::window {

// Registers the existing built-in page factories for an Alloy browser-process
// host. The renderer process must still use new_tab::CreateNewTabProcessApp().
bool RegisterAlloyBuiltinContentFactories(
    browser_new_tab::NewTabPageModel new_tab_model,
    browser_new_tab::NewTabPageStrings new_tab_strings,
    browser_mdv::MdvPageStrings mdv_strings,
    const std::shared_ptr<mdv::MdvRuntimeState>& mdv_state,
    CefRefPtr<CefRequestContext> request_context = nullptr);

class AlloyBuiltinContentObserver {
 public:
  virtual void OnBuiltinBrowserCreated(CefRefPtr<CefBrowser> browser) {
    static_cast<void>(browser);
  }
  virtual void OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                int http_status_code) {
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(http_status_code);
  }
  virtual void OnBuiltinLoadError(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  cef_errorcode_t error_code,
                                  const CefString& error_text,
                                  const CefString& failed_url) {
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(error_code);
    static_cast<void>(error_text);
    static_cast<void>(failed_url);
  }
  virtual void OnBuiltinTitleChange(CefRefPtr<CefBrowser> browser,
                                    const CefString& title) {
    static_cast<void>(browser);
    static_cast<void>(title);
  }
  virtual void OnBuiltinAddressChange(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      const CefString& url) {
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(url);
  }
  virtual void OnBuiltinLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                           bool is_loading,
                                           bool can_go_back,
                                           bool can_go_forward) {
    static_cast<void>(browser);
    static_cast<void>(is_loading);
    static_cast<void>(can_go_back);
    static_cast<void>(can_go_forward);
  }
  virtual bool OnBuiltinConsoleMessage(CefRefPtr<CefBrowser> browser,
                                       cef_log_severity_t level,
                                       const CefString& message,
                                       const CefString& source,
                                       int line) {
    static_cast<void>(browser);
    static_cast<void>(level);
    static_cast<void>(message);
    static_cast<void>(source);
    static_cast<void>(line);
    return false;
  }
  virtual void OnBuiltinBrowserClosing(CefRefPtr<CefBrowser> browser) {
    static_cast<void>(browser);
  }
  virtual void OnBuiltinRenderProcessTerminated(CefRefPtr<CefBrowser> browser) {
    static_cast<void>(browser);
  }
  virtual bool OnBuiltinProcessMessageReceived(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(source_process);
    static_cast<void>(message);
    return false;
  }
  virtual void OnBuiltinBeforeContextMenu(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(params);
    static_cast<void>(model);
  }
  virtual bool OnBuiltinContextMenuCommand(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefContextMenuParams> params, int command_id,
      cef_event_flags_t event_flags) {
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(params);
    static_cast<void>(command_id);
    static_cast<void>(event_flags);
    return false;
  }

 protected:
  virtual ~AlloyBuiltinContentObserver() = default;
};

// Runtime-neutral CefClient seam for the existing MDV entry/edit owners.
// It owns no document state and may be shared by multiple BrowserViews in one
// profile. All methods and Shutdown run on the CEF UI thread.
class AlloyBuiltinContent final : public CefClient,
                                  public CefLifeSpanHandler,
                                  public CefDisplayHandler,
                                  public CefLoadHandler,
                                  public CefRequestHandler,
                                  public CefKeyboardHandler,
                                  public CefContextMenuHandler {
 public:
  AlloyBuiltinContent(std::shared_ptr<mdv::MdvEntryController> entries,
                      std::shared_ptr<mdv::MdvEditController> editing,
                      AlloyBuiltinContentObserver* observer = nullptr);
  ~AlloyBuiltinContent() override;

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                        cef_log_severity_t level, const CefString& message,
                        const CefString& source, int line) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode error_code, const CefString& error_text,
                   const CefString& failed_url) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status, int error_code,
                                 const CefString& error_string) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event,
                  CefEventHandle os_event) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;

  void Shutdown();

 private:
  class QueryHandler;
  CefMessageRouterBrowserSide* EnsureRouter();

  std::shared_ptr<mdv::MdvEntryController> entries_;
  std::shared_ptr<mdv::MdvEditController> editing_;
  AlloyBuiltinContentObserver* observer_ = nullptr;
  std::unique_ptr<QueryHandler> query_handler_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  bool active_ = true;

  IMPLEMENT_REFCOUNTING(AlloyBuiltinContent);
  DISALLOW_COPY_AND_ASSIGN(AlloyBuiltinContent);
};

}  // namespace crayon::browser::cef_shell::window
