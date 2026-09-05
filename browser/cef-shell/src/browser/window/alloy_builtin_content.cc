#include "browser/window/alloy_builtin_content.h"

#include <utility>

#include "browser/new_tab/cef_new_tab_handler.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {

bool RegisterAlloyBuiltinContentFactories(
    browser_new_tab::NewTabPageModel new_tab_model,
    browser_new_tab::NewTabPageStrings new_tab_strings,
    browser_mdv::MdvPageStrings mdv_strings,
    const std::shared_ptr<mdv::MdvRuntimeState>& mdv_state) {
  CEF_REQUIRE_UI_THREAD();
  return mdv_state &&
         new_tab::RegisterNewTabSchemeHandlerFactory(
             std::move(new_tab_model), std::move(new_tab_strings)) &&
         mdv::RegisterMdvSchemeHandlerFactory(std::move(mdv_strings),
                                              mdv_state);
}

class AlloyBuiltinContent::QueryHandler final
    : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit QueryHandler(AlloyBuiltinContent* owner) : owner_(owner) {}

  bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
               int64_t query_id, const CefString& request, bool persistent,
               CefRefPtr<Callback> callback) override {
    return owner_ && owner_->active_ && owner_->editing_ &&
           owner_->editing_->OnPageQuery(browser, frame, query_id, request,
                                         persistent, std::move(callback));
  }

  void Detach() noexcept { owner_ = nullptr; }

 private:
  AlloyBuiltinContent* owner_;

};

AlloyBuiltinContent::AlloyBuiltinContent(
    std::shared_ptr<mdv::MdvEntryController> entries,
    std::shared_ptr<mdv::MdvEditController> editing,
    AlloyBuiltinContentObserver* observer)
    : entries_(std::move(entries)),
      editing_(std::move(editing)),
      observer_(observer) {
  if (entries_ && editing_) {
    entries_->SetDocumentLoadedCallback(
        [editing = editing_](CefRefPtr<CefBrowser> browser,
                             const std::string& path,
                             const std::string& normalized,
                             std::uint64_t size, std::uint64_t mtime) {
          editing->OnDocumentLoaded(browser, path, normalized, size, mtime);
        });
  }
}

AlloyBuiltinContent::~AlloyBuiltinContent() = default;

CefMessageRouterBrowserSide* AlloyBuiltinContent::EnsureRouter() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !editing_) {
    return nullptr;
  }
  if (!router_) {
    CefMessageRouterConfig config;
    config.js_query_function = "mdvQuery";
    router_ = CefMessageRouterBrowserSide::Create(config);
    query_handler_ = std::make_unique<QueryHandler>(this);
    router_->AddHandler(query_handler_.get(), true);
  }
  return router_.get();
}

void AlloyBuiltinContent::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinBrowserCreated(std::move(browser));
  }
}

void AlloyBuiltinContent::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (router_) {
    router_->OnBeforeClose(browser);
  }
  if (entries_) {
    entries_->CancelTransientEntries();
  }
  if (active_ && observer_) {
    observer_->OnBuiltinBrowserClosing(std::move(browser));
  }
}

void AlloyBuiltinContent::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                        const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinTitleChange(std::move(browser), title);
  }
}

void AlloyBuiltinContent::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinAddressChange(std::move(browser), std::move(frame),
                                      url);
  }
}

void AlloyBuiltinContent::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                               bool is_loading,
                                               bool can_go_back,
                                               bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinLoadingStateChange(std::move(browser), is_loading,
                                           can_go_back, can_go_forward);
  }
}

bool AlloyBuiltinContent::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                           cef_log_severity_t level,
                                           const CefString& message,
                                           const CefString& source,
                                           int line) {
  CEF_REQUIRE_UI_THREAD();
  return active_ && observer_ && observer_->OnBuiltinConsoleMessage(
                                     std::move(browser), level, message, source,
                                     line);
}

void AlloyBuiltinContent::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    int http_status_code) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinLoadEnd(std::move(browser), std::move(frame),
                                http_status_code);
  }
}

void AlloyBuiltinContent::OnLoadError(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      ErrorCode error_code,
                                      const CefString& error_text,
                                      const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinLoadError(std::move(browser), std::move(frame),
                                  error_code, error_text, failed_url);
  }
}

void AlloyBuiltinContent::OnRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code,
    const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(status);
  static_cast<void>(error_code);
  static_cast<void>(error_string);
  if (router_) {
    router_->OnRenderProcessTerminated(browser);
  }
  if (entries_) {
    entries_->CancelTransientEntries();
  }
  if (active_ && observer_) {
    observer_->OnBuiltinRenderProcessTerminated(std::move(browser));
  }
}

bool AlloyBuiltinContent::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefRequest> request,
                                         bool user_gesture,
                                         bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(is_redirect);
  if (!active_ || !request) {
    return false;
  }
  if (auto* router = EnsureRouter()) {
    router->OnBeforeBrowse(browser, frame);
  }
  const std::string url = request->GetURL().ToString();
  return (editing_ && editing_->InterceptWhileDirty(browser, url,
                                                     user_gesture)) ||
         (entries_ && entries_->InterceptNavigation(browser, request->GetURL(),
                                                     user_gesture));
}

bool AlloyBuiltinContent::OnKeyEvent(CefRefPtr<CefBrowser> browser,
                                     const CefKeyEvent& event,
                                     CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(os_event);
  constexpr int kForbiddenModifiers =
      EVENTFLAG_SHIFT_DOWN | EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN;
  return active_ && editing_ && event.type == KEYEVENT_KEYUP &&
         (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0 &&
         (event.modifiers & kForbiddenModifiers) == 0 &&
         (event.windows_key_code == 'S' || event.windows_key_code == 's') &&
         editing_->SaveWriteBack(std::move(browser));
}

bool AlloyBuiltinContent::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  auto* router = EnsureRouter();
  if (router && router->OnProcessMessageReceived(browser, frame, source_process,
                                                  message)) {
    return true;
  }
  return active_ && observer_ && observer_->OnBuiltinProcessMessageReceived(
                                     std::move(browser), std::move(frame),
                                     source_process, std::move(message));
}

void AlloyBuiltinContent::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && observer_) {
    observer_->OnBuiltinBeforeContextMenu(
        std::move(browser), std::move(frame), std::move(params),
        std::move(model));
  }
}

bool AlloyBuiltinContent::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, int command_id,
    EventFlags event_flags) {
  CEF_REQUIRE_UI_THREAD();
  return active_ && observer_ && observer_->OnBuiltinContextMenuCommand(
                                     std::move(browser), std::move(frame),
                                     std::move(params), command_id,
                                     event_flags);
}

void AlloyBuiltinContent::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) {
    return;
  }
  active_ = false;
  observer_ = nullptr;
  if (entries_) {
    entries_->CancelTransientEntries();
    entries_->SetDocumentLoadedCallback({});
  }
  if (router_ && query_handler_) {
    router_->RemoveHandler(query_handler_.get());
  }
  if (query_handler_) {
    query_handler_->Detach();
  }
  query_handler_.reset();
  router_ = nullptr;
  editing_.reset();
  entries_.reset();
}

}  // namespace crayon::browser::cef_shell::window
