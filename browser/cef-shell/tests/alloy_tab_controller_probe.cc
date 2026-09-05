#include "alloy_tab_controller_probe.h"

#include <windows.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "browser/window/alloy_tab_controller.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_request_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyTabController;
using crayon::browser::cef_shell::window::TabId;
using crayon::browser::cef_shell::window::TabLifecycle;
using namespace crayon::browser_engine;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 600;
constexpr char kLateUrl[] = "data:text/html,<title>late-ready</title>late";

template <typename T> T Required(std::optional<T> value) {
  if (!value.has_value()) {
    std::abort();
  }
  return std::move(*value);
}

class AlloyTabProbe final : public CefApp,
                            public CefBrowserProcessHandler,
                            public CefClient,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefDisplayHandler,
                            public CefRequestHandler,
                            public CefJSDialogHandler,
                            public CefBrowserViewDelegate,
                            public CefWindowDelegate {
public:
  AlloyTabProbe(std::string fixture_url,
                std::shared_ptr<AlloyTabControllerProbeResult> result)
      : fixture_url_(std::move(fixture_url)), result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }

  void
  OnBeforeCommandLineProcessing(const CefString &,
                                CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-default-apps");
    command->AppendSwitch("disable-sync");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    controller_ = std::make_unique<AlloyTabController>(
        Required(ProfileId::TryCreate("alloy-tab-probe")), 3);
    CefBrowserSettings settings;
    views_[0] = CefBrowserView::CreateBrowserView(this, fixture_url_, settings,
                                                  nullptr, nullptr, this);
    views_[1] = CefBrowserView::CreateBrowserView(this, kLateUrl, settings,
                                                  nullptr, nullptr, this);
    first_tab_ = controller_->BeginCreate(views_[0], ContentPurpose::kWeb,
                                          NavigationId::FromRaw(1));
    late_tab_ = controller_->BeginCreate(views_[1], ContentPurpose::kWeb,
                                         NavigationId::FromRaw(1));
    if (!first_tab_.has_value() || !late_tab_.has_value() ||
        !controller_->RequestClose(*late_tab_, true)) {
      Finish(false, "initial-create");
      return;
    }
    CefWindow::CreateTopLevelWindow(this);
    ScheduleCheck();
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(controller_->container());
    layout->SetFlexForView(controller_->container(), 1);
    window_->SetSize(CefSize(800, 560));
    window_->Layout();
    window_->Show();
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                        CefRefPtr<CefBrowser> browser) override {
    int index = ViewIndex(view);
    if (index < 0) {
      Finish(false, "unknown-created-view");
      return;
    }
    browsers_[index] = browser;
    const auto capabilities = Required(ContentCapabilitySet::TryCreate(
        (1U << static_cast<unsigned>(ContentCapability::kNavigate)) |
        (1U << static_cast<unsigned>(ContentCapability::kZoom))));
    if (!controller_->OnBrowserCreated(view, browser, capabilities)) {
      const auto expected = index == 0 ? first_tab_ : std::nullopt;
      const auto *tab =
          expected.has_value() ? controller_->model().Find(*expected) : nullptr;
      std::cout << "alloy_tab_controller_windows created_failure index="
                << index << " runtime=" << browser->GetHost()->GetRuntimeStyle()
                << " lifecycle="
                << (tab ? static_cast<int>(tab->lifecycle) : -1) << std::endl;
      Finish(false, "created-callback");
      return;
    }
    const std::optional<TabId> tab_id = index == 0 ? first_tab_ : std::nullopt;
    if (tab_id.has_value()) {
      CefPostTask(TID_UI,
                  base::BindOnce(&AlloyTabProbe::ActivateCreated,
                                 CefRefPtr<AlloyTabProbe>(this), *tab_id));
    }
  }

  bool DoClose(CefRefPtr<CefBrowser> browser) override {
    const bool handled = controller_->OnDoClose(browser);
    if (handled) {
      CefPostTask(TID_UI,
                  base::BindOnce(&AlloyTabProbe::ReleaseClosingView,
                                 CefRefPtr<AlloyTabProbe>(this), browser));
    }
    return handled;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    const int index = BrowserIndex(browser);
    if (!controller_->OnBeforeClose(browser)) {
      Finish(false, "before-close");
      return;
    }
    if (index == 1) {
      result_->late_create_closed = true;
    } else if (crashing_browser_ && crashing_browser_->IsSame(browser)) {
      result_->renderer_crash_closed = true;
      crashing_browser_ = nullptr;
    }
    if (index >= 0) {
      browsers_[index] = nullptr;
    }
    if (controller_->pending_count() == 0 && window_) {
      if (!finished_) {
        finished_ = true;
        passed_ = result_->close_cancelled && state_preserved_ &&
                  result_->late_create_closed && result_->renderer_crash_closed;
        std::cout << "alloy_tab_controller_windows detail=complete"
                  << std::endl;
      }
      if (!controller_->ReleaseAfterClosed()) {
        passed_ = false;
      }
      window_->Close();
    }
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> view,
                          CefRefPtr<CefBrowser>) override {
    const int index = ViewIndex(view);
    if (index >= 0) {
      views_[index] = nullptr;
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int) override {
    if (frame->IsMain()) {
      const int index = BrowserIndex(browser);
      if (index >= 0) {
        if (index == 0) {
          frame->ExecuteJavaScript(
              "window.value=42;"
              "document.addEventListener('click',function(){"
              "window.onbeforeunload=function(e){e.preventDefault();"
              "e.returnValue='';};document.title='close-fixture-armed';});",
              fixture_url_, 1);
        }
        loaded_[index] = true;
      }
    }
  }

  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString &title) override {
    if (title == "state-preserved") {
      state_preserved_ = true;
    } else if (title == "close-fixture-armed") {
      clicked_ = true;
    } else if (title == "state-lost") {
      Finish(false, "state-lost");
    }
  }

  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString &,
                            bool,
                            CefRefPtr<CefJSDialogCallback> callback) override {
    if (result_->close_cancelled) {
      callback->Continue(true, CefString());
      return true;
    }
    if (!first_tab_.has_value() || !browsers_[0] ||
        !browsers_[0]->IsSame(browser)) {
      return false;
    }
    callback->Continue(false, CefString());
    result_->close_cancelled = controller_->CancelClose(*first_tab_);
    return true;
  }

  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus, int,
                                 const CefString &) override {
    if (!controller_->OnRenderProcessGone(browser)) {
      Finish(false, "renderer-gone");
      return;
    }
    CefPostTask(TID_UI,
                base::BindOnce(&AlloyTabProbe::FinalizeRendererCrash,
                               CefRefPtr<AlloyTabProbe>(this), browser));
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    ++can_close_calls_;
    if (first_tab_.has_value() && browsers_[0]) {
      const auto *tab = controller_->model().Find(*first_tab_);
      if (tab && tab->lifecycle == TabLifecycle::kClosing) {
        if (!cancel_posted_) {
          cancel_posted_ = true;
          CefPostTask(TID_UI,
                      base::BindOnce(&AlloyTabProbe::ApplyCloseCancellation,
                                     CefRefPtr<AlloyTabProbe>(this)));
        }
        return false;
      }
    }
    return controller_->pending_count() == 0;
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    result_->behavior_passed = passed_ && result_->close_cancelled &&
                               result_->late_create_closed &&
                               result_->renderer_crash_closed &&
                               result_->advanced_state_passed;
    std::cout << "alloy_tab_controller_windows passed="
              << result_->behavior_passed
              << " close_cancelled=" << result_->close_cancelled
              << " late_create_closed=" << result_->late_create_closed
              << " renderer_crash_closed=" << result_->renderer_crash_closed
              << " advanced_state=" << result_->advanced_state_passed
              << std::endl;
    window_ = nullptr;
    controller_.reset();
    CefQuitMessageLoop();
  }

private:
  int ViewIndex(CefRefPtr<CefBrowserView> view) const {
    for (int index = 0; index < 3; ++index) {
      if (views_[index] && views_[index]->IsSame(view)) {
        return index;
      }
    }
    return -1;
  }

  int BrowserIndex(CefRefPtr<CefBrowser> browser) const {
    for (int index = 0; index < 3; ++index) {
      if (browsers_[index] && browsers_[index]->IsSame(browser)) {
        return index;
      }
    }
    return -1;
  }

  void ScheduleCheck() {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&AlloyTabProbe::Check, CefRefPtr<AlloyTabProbe>(this)),
        kPollMilliseconds);
  }

  void ReleaseClosingView(CefRefPtr<CefBrowser> browser) {
    const int index = BrowserIndex(browser);
    if (!controller_->ReleaseAfterDoClose(browser)) {
      Finish(false, "release-after-do-close");
      return;
    }
    if (index >= 0) {
      views_[index] = nullptr;
    }
  }

  void ActivateCreated(TabId tab_id) {
    if (!controller_->Activate(tab_id)) {
      Finish(false, "activate-created");
    }
  }

  void ApplyCloseCancellation() {
    result_->close_cancelled =
        first_tab_.has_value() && controller_->CancelClose(*first_tab_);
    if (!result_->close_cancelled) {
      Finish(false, "apply-close-cancellation");
    }
  }

  void FinalizeRendererCrash(CefRefPtr<CefBrowser> browser) {
    const int index = BrowserIndex(browser);
    if (!controller_->FinalizeRendererCrash(browser)) {
      Finish(false, "finalize-renderer-crash");
      return;
    }
    if (index >= 0) {
      views_[index] = nullptr;
    }
  }

  void Check() {
    if (finished_) {
      return;
    }
    if (logged_stage_ != stage_) {
      logged_stage_ = stage_;
      std::cout << "alloy_tab_controller_windows stage=" << stage_ << std::endl;
    }
    if (++checks_ > kMaximumChecks) {
      std::cout << "alloy_tab_controller_windows timeout_stage=" << stage_
                << " clicked=" << clicked_
                << " can_close_calls=" << can_close_calls_
                << " first_browser=" << static_cast<bool>(browsers_[0])
                << std::endl;
      Finish(false, "timeout");
      return;
    }
    if (stage_ == 0) {
      if (!loaded_[0] || !result_->late_create_closed) {
        ScheduleCheck();
        return;
      }
      const auto *tab = controller_->model().Find(*first_tab_);
      if (!tab || tab->lifecycle != TabLifecycle::kReady ||
          controller_->model().size() != 1 || !window_->IsVisible() ||
          window_->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY) {
        Finish(false, "ready-owner");
        return;
      }
      const auto matches = controller_->SearchTabs("tab-1");
      result_->advanced_state_passed =
          controller_->PinTab(*first_tab_, true) &&
          controller_->MuteTab(*first_tab_, true) &&
          controller_->SetTabGroup(*first_tab_, std::string("group-a")) &&
          controller_->IsPinned(*first_tab_) &&
          controller_->IsMuted(*first_tab_) &&
          controller_->TabGroup(*first_tab_) ==
              std::optional<std::string>("group-a") &&
          matches.size() == 1 && matches.front() == *first_tab_ &&
          controller_->advanced_ordered_tabs() ==
              std::vector<TabId>{*first_tab_} &&
          browsers_[0]->GetHost()->IsAudioMuted() &&
          controller_->MuteTab(*first_tab_, false) &&
          !browsers_[0]->GetHost()->IsAudioMuted();
      if (!result_->advanced_state_passed) {
        Finish(false, "advanced-state");
        return;
      }
      const HWND handle = window_->GetWindowHandle();
      RECT bounds{};
      window_->Activate();
      views_[0]->RequestFocus();
      browsers_[0]->GetHost()->SetFocus(true);
      if (!handle || !GetWindowRect(handle, &bounds) ||
          !SetForegroundWindow(handle) ||
          !SetCursorPos(bounds.left + 50, bounds.top + 50)) {
        Finish(false, "activate-window");
        return;
      }
      INPUT input[2]{};
      input[0].type = INPUT_MOUSE;
      input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
      input[1].type = INPUT_MOUSE;
      input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
      if (SendInput(2, input, sizeof(INPUT)) != 2) {
        Finish(false, "activate-input");
        return;
      }
      activation_check_ = checks_;
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 1) {
      if (!clicked_ || checks_ - activation_check_ < 5) {
        ScheduleCheck();
        return;
      }
      if (!controller_->BeginClose(*first_tab_)) {
        Finish(false, "close-request");
        return;
      }
      cancel_posted_ = true;
      CefPostTask(TID_UI, base::BindOnce(&AlloyTabProbe::ApplyCloseCancellation,
                                         CefRefPtr<AlloyTabProbe>(this)));
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 2) {
      if (!result_->close_cancelled) {
        ScheduleCheck();
        return;
      }
      const auto *tab = controller_->model().Find(*first_tab_);
      if (!tab || tab->lifecycle != TabLifecycle::kReady ||
          !controller_->Activate(*first_tab_)) {
        Finish(false, "cancel-restore");
        return;
      }
      browsers_[0]->GetMainFrame()->ExecuteJavaScript(
          "document.title=window.value===42?'state-preserved':'state-lost';",
          fixture_url_, 1);
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 3) {
      if (!state_preserved_) {
        ScheduleCheck();
        return;
      }
      crashing_browser_ = browsers_[0];
      if (!controller_->OnRenderProcessGone(crashing_browser_)) {
        Finish(false, "renderer-gone-transition");
        return;
      }
      passed_ = result_->close_cancelled && state_preserved_ &&
                result_->late_create_closed &&
                result_->advanced_state_passed;
      CefPostTask(TID_UI, base::BindOnce(&AlloyTabProbe::FinalizeRendererCrash,
                                         CefRefPtr<AlloyTabProbe>(this),
                                         crashing_browser_));
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (!result_->renderer_crash_closed) {
      ScheduleCheck();
      return;
    }
  }

  void Finish(bool passed, const char *detail) {
    if (finished_) {
      return;
    }
    finished_ = true;
    passed_ = passed;
    std::cout << "alloy_tab_controller_windows detail=" << detail << std::endl;
    if (!controller_) {
      CefQuitMessageLoop();
      return;
    }
    controller_->CloseAll(true);
    if (controller_->pending_count() == 0 && window_) {
      controller_->ReleaseAfterClosed();
      window_->Close();
    }
  }

  std::shared_ptr<AlloyTabControllerProbeResult> result_;
  const std::string fixture_url_;
  std::unique_ptr<AlloyTabController> controller_;
  std::array<CefRefPtr<CefBrowserView>, 3> views_;
  std::array<CefRefPtr<CefBrowser>, 3> browsers_;
  CefRefPtr<CefBrowser> crashing_browser_;
  std::array<bool, 3> loaded_{};
  std::optional<TabId> first_tab_;
  std::optional<TabId> late_tab_;
  CefRefPtr<CefWindow> window_;
  int checks_ = 0;
  int activation_check_ = 0;
  int can_close_calls_ = 0;
  int stage_ = 0;
  int logged_stage_ = -1;
  bool state_preserved_ = false;
  bool clicked_ = false;
  bool cancel_posted_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(AlloyTabProbe);
};

} // namespace

CefRefPtr<CefApp> CreateAlloyTabControllerProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyTabControllerProbeResult> result) {
  return new AlloyTabProbe(std::move(fixture_url), std::move(result));
}
