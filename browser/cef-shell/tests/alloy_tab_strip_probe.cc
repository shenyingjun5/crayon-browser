#include "alloy_tab_strip_probe.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <optional>
#include <utility>

#include "browser/window/alloy_tab_strip.h"
#include "browser/window/tab_model.h"
#include "include/base/cef_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_display.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace {

using crayon::browser::cef_shell::window::AlloyTabStrip;
using crayon::browser::cef_shell::window::kMaximumTabsPerWindow;
using crayon::browser::cef_shell::window::TabId;
using crayon::browser::cef_shell::window::TabModel;

constexpr int kPollMilliseconds = 25;
constexpr int kMaximumChecks = 200;

class AlloyTabStripProbe final : public CefApp,
                                 public CefBrowserProcessHandler,
                                 public CefWindowDelegate {
public:
  explicit AlloyTabStripProbe(std::shared_ptr<AlloyTabStripProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

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
    first_ = AddBoundTab();
    second_ = AddBoundTab();
    third_ = AddBoundTab();
    if (!first_ || !second_ || !third_ || !model_.Activate(*first_)) {
      Finish(false, "initial-model");
      return;
    }
    strip_ = std::make_unique<AlloyTabStrip>(
        AlloyTabStrip::Strings{"New tab", "Close tab", "Tab"},
        AlloyTabStrip::Callbacks{
            [this]() { OnNewTab(); },
            [this](TabId tab_id) { OnActivateTab(tab_id); },
            [this](TabId tab_id) { OnCloseTab(tab_id); }});
    if (!strip_->Sync(model_)) {
      Finish(false, "initial-sync");
      return;
    }
    CefWindow::CreateTopLevelWindow(this);
  }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    mounted_panel_ = strip_->panel();
    window_->AddChildView(mounted_panel_);
    layout->SetFlexForView(mounted_panel_, 1);
    window_->SetTitle("Crayon Alloy Tab Strip Probe");
    window_->SetSize(CefSize(900, 180));
    window_->Layout();
    window_->Show();
    window_->Activate();
    ScheduleCheck();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return finished_; }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    result_->behavior_passed = passed_ && result_->real_clicks_passed &&
                               result_->capacity_passed &&
                               result_->layout_passed && result_->icons_passed;
    std::cout << "alloy_tab_strip_windows passed=" << result_->behavior_passed
              << " real_clicks=" << result_->real_clicks_passed
              << " capacity=" << result_->capacity_passed
              << " layout=" << result_->layout_passed
              << " icons=" << result_->icons_passed << std::endl;
    mounted_panel_ = nullptr;
    window_ = nullptr;
    strip_.reset();
    CefQuitMessageLoop();
  }

private:
  std::optional<TabId> AddBoundTab() {
    const auto tab_id = model_.CreateTab();
    if (!tab_id || !model_.BindBrowser(*tab_id, next_browser_id_++)) {
      return std::nullopt;
    }
    return tab_id;
  }

  void OnNewTab() {
    ++new_events_;
    last_new_ = AddBoundTab();
    PostSync();
  }

  void OnActivateTab(TabId tab_id) {
    ++activate_events_;
    last_activated_ = tab_id;
    if (!model_.Activate(tab_id)) {
      callback_failed_ = true;
    }
    PostSync();
  }

  void OnCloseTab(TabId tab_id) {
    ++close_events_;
    last_closed_ = tab_id;
    const auto *tab = model_.Find(tab_id);
    const int browser_id = tab ? tab->browser_id : 0;
    if (!model_.RequestClose(tab_id) || browser_id <= 0 ||
        !model_.DetachBrowser(browser_id)) {
      callback_failed_ = true;
    }
    PostSync();
  }

  void PostSync() {
    CefPostTask(TID_UI, base::BindOnce(&AlloyTabStripProbe::ApplySync,
                                       CefRefPtr<AlloyTabStripProbe>(this)));
  }

  void ApplySync() {
    if (finished_ || !strip_ || !strip_->Sync(model_)) {
      Finish(false, "async-sync");
      return;
    }
    if (window_) {
      window_->Layout();
    }
  }

  bool Click(int command_id) {
    if (!window_ || !strip_ || !strip_->panel()) {
      return false;
    }
    const auto view = strip_->panel()->GetViewForID(command_id);
    if (!view || !view->IsDrawn()) {
      return false;
    }
    const CefRect bounds = view->GetBoundsInScreen();
    if (bounds.width <= 0 || bounds.height <= 0) {
      return false;
    }
    window_->Activate();
    view->RequestFocus();
    const HWND handle = window_->GetWindowHandle();
    const CefPoint center = CefDisplay::ConvertScreenPointToPixels(
        CefPoint(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2));
    if (!handle ||
        (GetForegroundWindow() != handle &&
         (!SetForegroundWindow(handle) || GetForegroundWindow() != handle)) ||
        !SetCursorPos(center.x, center.y)) {
      return false;
    }
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
  }

  bool ProjectionMatches(std::initializer_list<TabId> expected,
                         std::optional<TabId> active) const {
    if (!strip_ || strip_->rendered_tab_count() != expected.size() ||
        strip_->active_rendered_tab() != active) {
      return false;
    }
    std::size_t index = 0;
    for (TabId tab_id : expected) {
      if (strip_->rendered_tab_at(index++) != tab_id) {
        return false;
      }
    }
    return true;
  }

  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&AlloyTabStripProbe::Check,
                                      CefRefPtr<AlloyTabStripProbe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_) {
      return;
    }
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (callback_failed_) {
      Finish(false, "callback-model");
      return;
    }
    if (logged_stage_ != stage_) {
      logged_stage_ = stage_;
      std::cout << "alloy_tab_strip_windows stage=" << stage_ << std::endl;
    }
    if (stage_ == 0) {
      if (!window_->IsVisible() ||
          window_->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY ||
          !ProjectionMatches({*first_, *second_, *third_}, first_) ||
          !strip_->Sync(model_)) {
        Finish(false, "initial-projection");
        return;
      }
      const auto active =
          strip_->panel()
              ->GetViewForID(AlloyTabStrip::kActivateCommandBase)
              ->AsButton();
      const auto close = strip_->panel()
                             ->GetViewForID(AlloyTabStrip::kCloseCommandBase)
                             ->AsButton()
                             ->AsLabelButton();
      const auto add = strip_->panel()
                           ->GetViewForID(AlloyTabStrip::kNewTabCommandId)
                           ->AsButton()
                           ->AsLabelButton();
      result_->icons_passed =
          active && active->IsEnabled() && close && add &&
          close->GetText().empty() && add->GetText().empty() &&
          close->GetImage(CEF_BUTTON_STATE_NORMAL) &&
          !close->GetImage(CEF_BUTTON_STATE_NORMAL)->IsEmpty() &&
          close->GetImage(CEF_BUTTON_STATE_NORMAL)->HasRepresentation(1.0F) &&
          close->GetImage(CEF_BUTTON_STATE_NORMAL)->HasRepresentation(2.0F) &&
          add->GetImage(CEF_BUTTON_STATE_NORMAL) &&
          !add->GetImage(CEF_BUTTON_STATE_NORMAL)->IsEmpty() &&
          close->IsFocusable() && add->IsFocusable();
      if (!result_->icons_passed) {
        Finish(false, "icon-contract");
        return;
      }
      window_->SetSize(CefSize(320, 140));
      window_->Layout();
      const CefRect narrow = mounted_panel_->GetBoundsInScreen();
      window_->SetSize(CefSize(1000, 220));
      window_->Layout();
      const CefRect wide = mounted_panel_->GetBoundsInScreen();
      result_->layout_passed = narrow.width > 0 && narrow.height > 0 &&
                               wide.width > narrow.width && wide.height > 0;
      if (!result_->layout_passed ||
          !Click(AlloyTabStrip::kActivateCommandBase + 1)) {
        Finish(false, "layout-or-activate-click");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 1) {
      if (activate_events_ == 0) {
        ScheduleCheck();
        return;
      }
      if (activate_events_ != 1 || last_activated_ != second_ ||
          !ProjectionMatches({*first_, *second_, *third_}, second_) ||
          !strip_->Sync(model_, {*third_, *first_, *second_}) ||
          !ProjectionMatches({*third_, *first_, *second_}, second_) ||
          strip_->Sync(model_, {*third_, *third_, *second_}) ||
          !ProjectionMatches({*third_, *first_, *second_}, second_) ||
          !model_.MoveTab(2, 0) || !strip_->Sync(model_) ||
          !ProjectionMatches({*third_, *first_, *second_}, second_)) {
        Finish(false, "activate-or-reorder");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 2) {
      window_->Layout();
      if (!Click(AlloyTabStrip::kCloseCommandBase + 2)) {
        Finish(false, "close-click");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 3) {
      if (close_events_ == 0 ||
          !ProjectionMatches({*third_, *first_}, first_)) {
        ScheduleCheck();
        return;
      }
      if (close_events_ != 1 || last_closed_ != second_) {
        Finish(false, "close-projection");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 4) {
      window_->Layout();
      if (!Click(AlloyTabStrip::kNewTabCommandId)) {
        Finish(false, "new-click");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 5) {
      if (new_events_ == 0 || !last_new_ ||
          !ProjectionMatches({*third_, *first_, *last_new_}, last_new_)) {
        ScheduleCheck();
        return;
      }
      if (new_events_ != 1) {
        Finish(false, "new-projection");
        return;
      }
      while (model_.size() < kMaximumTabsPerWindow) {
        if (!AddBoundTab()) {
          Finish(false, "capacity-fill");
          return;
        }
      }
      if (!strip_->Sync(model_) || strip_->new_tab_enabled() ||
          strip_->rendered_tab_count() != kMaximumTabsPerWindow) {
        Finish(false, "capacity-projection");
        return;
      }
      result_->capacity_passed = true;
      disabled_click_baseline_ = new_events_;
      if (!Click(AlloyTabStrip::kNewTabCommandId)) {
        Finish(false, "disabled-new-click");
        return;
      }
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (new_events_ != disabled_click_baseline_) {
      Finish(false, "disabled-new-dispatched");
      return;
    }
    result_->real_clicks_passed =
        activate_events_ == 1 && close_events_ == 1 && new_events_ == 1;
    Finish(result_->real_clicks_passed, "complete");
  }

  void Finish(bool passed, const char *detail) {
    if (finished_) {
      return;
    }
    finished_ = true;
    passed_ = passed;
    std::cout << "alloy_tab_strip_windows detail=" << detail << std::endl;
    if (strip_) {
      if (!strip_->Shutdown() || !strip_->Shutdown()) {
        passed_ = false;
      }
    }
    if (window_) {
      if (mounted_panel_) {
        window_->RemoveChildView(mounted_panel_);
      }
      window_->Close();
    } else {
      CefQuitMessageLoop();
    }
  }

  std::shared_ptr<AlloyTabStripProbeResult> result_;
  TabModel model_;
  std::unique_ptr<AlloyTabStrip> strip_;
  CefRefPtr<CefPanel> mounted_panel_;
  CefRefPtr<CefWindow> window_;
  std::optional<TabId> first_;
  std::optional<TabId> second_;
  std::optional<TabId> third_;
  std::optional<TabId> last_new_;
  std::optional<TabId> last_activated_;
  std::optional<TabId> last_closed_;
  int next_browser_id_ = 101;
  int checks_ = 0;
  int stage_ = 0;
  int logged_stage_ = -1;
  int new_events_ = 0;
  int activate_events_ = 0;
  int close_events_ = 0;
  int disabled_click_baseline_ = 0;
  bool callback_failed_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(AlloyTabStripProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloyTabStripProbe(std::shared_ptr<AlloyTabStripProbeResult> result) {
  return new AlloyTabStripProbe(std::move(result));
}
