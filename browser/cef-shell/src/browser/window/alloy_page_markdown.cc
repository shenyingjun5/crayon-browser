#include "browser/window/alloy_page_markdown.h"

#include <utility>

#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

bool IsHttpUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

}  // namespace

AlloyPageMarkdown::AlloyPageMarkdown(
    AlloyTabController* tabs,
    std::shared_ptr<mdv::MdvEditController> mdv_editing,
    page_markdown::PageMarkdownStrings strings,
    std::function<bool(const std::string&)> clipboard_write,
    AlloyBuiltinContentObserver* downstream)
    : tabs_(tabs),
      downstream_(downstream),
      preview_(
          page_markdown::PageMarkdownSnapshotHost{
              [this](int browser_id) { return Lookup(browser_id); },
              [this](CefRefPtr<CefBrowser> browser) {
                return Start(std::move(browser));
              },
              [this](const browser_engine::SnapshotRequestId& request_id) {
                Cancel(request_id);
              }},
          std::move(mdv_editing), std::move(strings),
          std::move(clipboard_write)) {
  CEF_REQUIRE_UI_THREAD();
}

AlloyPageMarkdown::~AlloyPageMarkdown() = default;

void AlloyPageMarkdown::SetSnapshotObserver(
    gateway::PageSnapshotObserver* observer) {
  CEF_REQUIRE_UI_THREAD();
  if (active_) bridge_.SetObserver(observer);
}

void AlloyPageMarkdown::SetSnapshotAdmission(std::function<bool()> admission) {
  CEF_REQUIRE_UI_THREAD();
  if (active_) admission_ = std::move(admission);
}

void AlloyPageMarkdown::SetEventsReadyCallback(
    std::function<void()> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (active_) events_ready_ = std::move(callback);
}

std::vector<gateway::SnapshotGatewayEvent>
AlloyPageMarkdown::DrainSnapshots(std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  return active_ ? bridge_.Drain(max_events)
                 : std::vector<gateway::SnapshotGatewayEvent>{};
}

void AlloyPageMarkdown::Tick(
    std::vector<::crayon::cef_shell::ipc::content_host::Message> replies,
    bool content_host_healthy) {
  CEF_REQUIRE_UI_THREAD();
  if (active_) preview_.Tick(std::move(replies), content_host_healthy);
}

void AlloyPageMarkdown::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) return;
  active_ = false;
  preview_.Stop();
  bridge_.ShutDown();
  bridge_.SetObserver(nullptr);
  events_ready_ = {};
  admission_ = {};
  downstream_ = nullptr;
  tabs_ = nullptr;
}

void AlloyPageMarkdown::OnBuiltinBrowserCreated(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && downstream_) {
    downstream_->OnBuiltinBrowserCreated(std::move(browser));
  }
}

void AlloyPageMarkdown::OnBuiltinLoadEnd(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         int http_status_code) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && downstream_) {
    downstream_->OnBuiltinLoadEnd(std::move(browser), std::move(frame),
                                  http_status_code);
  }
}

void AlloyPageMarkdown::OnBuiltinLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    cef_errorcode_t error_code, const CefString& error_text,
    const CefString& failed_url) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && downstream_) {
    downstream_->OnBuiltinLoadError(std::move(browser), std::move(frame),
                                    error_code, error_text, failed_url);
  }
}

void AlloyPageMarkdown::OnBuiltinTitleChange(CefRefPtr<CefBrowser> browser,
                                             const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && downstream_) {
    downstream_->OnBuiltinTitleChange(std::move(browser), title);
  }
}

void AlloyPageMarkdown::OnBuiltinAddressChange(CefRefPtr<CefBrowser> browser,
                                               CefRefPtr<CefFrame> frame,
                                               const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && tabs_ && frame && frame->IsMain()) {
    static_cast<void>(tabs_->OnAddressChange(browser, url.ToString()));
  }
  if (active_ && downstream_) {
    downstream_->OnBuiltinAddressChange(std::move(browser), std::move(frame),
                                        url);
  }
}

void AlloyPageMarkdown::OnBuiltinLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
    bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  std::uint64_t before_generation = 0;
  if (active_ && tabs_ && browser) {
    const auto before = Lookup(browser->GetIdentifier());
    if (before) before_generation = before->navigation_id;
    if (tabs_->OnLoadingStateChange(browser, is_loading, can_go_back,
                                    can_go_forward)) {
      const auto after = Lookup(browser->GetIdentifier());
      if (after && after->navigation_id != before_generation) {
        bridge_.AdvanceNavigation(browser, after->tab_id,
                                  after->navigation_id);
        NotifyEventsReady();
      }
    }
  }
  if (active_ && downstream_) {
    downstream_->OnBuiltinLoadingStateChange(std::move(browser), is_loading,
                                             can_go_back, can_go_forward);
  }
}

bool AlloyPageMarkdown::OnBuiltinConsoleMessage(
    CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
    const CefString& message, const CefString& source, int line) {
  CEF_REQUIRE_UI_THREAD();
  return active_ && downstream_ && downstream_->OnBuiltinConsoleMessage(
                                      std::move(browser), level, message,
                                      source, line);
}

void AlloyPageMarkdown::OnBuiltinBrowserClosing(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && tabs_ && browser) {
    const auto tab = Lookup(browser->GetIdentifier());
    if (tab) {
      bridge_.CloseBrowser(browser, tab->tab_id);
      NotifyEventsReady();
    }
    preview_.Stop();
  }
  if (active_ && downstream_) {
    downstream_->OnBuiltinBrowserClosing(std::move(browser));
  }
}

void AlloyPageMarkdown::OnBuiltinRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && tabs_ && browser) {
    const auto tab = Lookup(browser->GetIdentifier());
    if (tab) {
      bridge_.RendererGone(browser, tab->tab_id);
      NotifyEventsReady();
    }
    preview_.Stop();
  }
  if (active_ && downstream_) {
    downstream_->OnBuiltinRenderProcessTerminated(std::move(browser));
  }
}

bool AlloyPageMarkdown::OnBuiltinProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && bridge_.OnProcessMessageReceived(
                     browser, frame, source_process, message)) {
    NotifyEventsReady();
    return true;
  }
  return active_ && downstream_ &&
         downstream_->OnBuiltinProcessMessageReceived(
             std::move(browser), std::move(frame), source_process,
             std::move(message));
}

void AlloyPageMarkdown::OnBuiltinBeforeContextMenu(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  if (active_) {
    static_cast<void>(
        preview_.HandleContextMenuAugment(browser, params, model));
  }
  if (active_ && downstream_) {
    downstream_->OnBuiltinBeforeContextMenu(std::move(browser),
                                            std::move(frame),
                                            std::move(params),
                                            std::move(model));
  }
}

bool AlloyPageMarkdown::OnBuiltinContextMenuCommand(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params, int command_id,
    cef_event_flags_t event_flags) {
  CEF_REQUIRE_UI_THREAD();
  if (active_ && preview_.HandleContextMenuCommand(browser, command_id)) {
    return true;
  }
  return active_ && downstream_ && downstream_->OnBuiltinContextMenuCommand(
                                      std::move(browser), std::move(frame),
                                      std::move(params), command_id,
                                      event_flags);
}

std::optional<page_markdown::PageMarkdownTabState>
AlloyPageMarkdown::Lookup(int browser_id) const {
  if (!active_ || !tabs_) return std::nullopt;
  const TabSnapshot* tab = tabs_->model().FindByBrowser(browser_id);
  if (!tab) return std::nullopt;
  return page_markdown::PageMarkdownTabState{
      tab->browser_id, tab->id, tab->navigation_generation, tab->url,
      tab->lifecycle == TabLifecycle::kReady, tab->loading};
}

std::optional<browser_engine::SnapshotRequestId> AlloyPageMarkdown::Start(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !browser || (admission_ && !admission_())) {
    return std::nullopt;
  }
  const auto tab = Lookup(browser->GetIdentifier());
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!tab || !tab->ready || tab->loading || tab->navigation_id == 0 ||
      !frame || !frame->IsMain() || !IsHttpUrl(tab->url) ||
      frame->GetURL().ToString() != tab->url) {
    return std::nullopt;
  }
  return bridge_.StartSnapshot(browser, tab->tab_id, tab->navigation_id,
                               browser_engine::SnapshotMode::kStandard);
}

void AlloyPageMarkdown::Cancel(
    const browser_engine::SnapshotRequestId& request_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) return;
  if (bridge_.CancelSnapshot(request_id) ==
      gateway::SnapshotGatewayResult::kAccepted) {
    NotifyEventsReady();
  }
}

void AlloyPageMarkdown::NotifyEventsReady() {
  if (active_ && events_ready_) events_ready_();
}

}  // namespace crayon::browser::cef_shell::window
