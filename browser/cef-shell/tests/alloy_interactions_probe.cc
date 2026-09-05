#include "alloy_interactions_probe.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "browser/branding/about_destination.h"
#include "browser/window/alloy_interactions.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyInteractions;
using crayon::browser::cef_shell::window::AlloyMainCommand;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 400;
constexpr int kContextCommandId = MENU_ID_USER_FIRST + 77;

class Probe final : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefClient,
                    public CefLifeSpanHandler,
                    public CefLoadHandler,
                    public CefBrowserViewDelegate,
                    public CefWindowDelegate {
public:
  explicit Probe(std::shared_ptr<AlloyInteractionsProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return interactions_;
  }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return interactions_; }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  void OnBeforeCommandLineProcessing(const CefString &,
                                     CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("no-proxy-server");
  }
  void OnContextInitialized() override {
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, "about:blank", settings,
                                              nullptr, nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
    browser_ = nullptr;
    if (window_)
      window_->Close();
  }
  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                 int) override {
    if (frame->IsMain())
      loaded_ = true;
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings column;
    column.horizontal = false;
    auto window_layout = window_->SetToBoxLayout(column);
    toolbar_ = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings row;
    row.horizontal = true;
    toolbar_->SetToBoxLayout(row);
    window_->AddChildView(toolbar_);
    window_->AddChildView(view_);
    window_layout->SetFlexForView(view_, 1);
    window_->SetSize(CefSize(720, 480));
    window_->Layout();
    window_->Show();
  }
  bool OnAccelerator(CefRefPtr<CefWindow>, int command_id) override {
    return interactions_ &&
           interactions_->HandleAccelerator(
               command_id, static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN));
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    return finished_ &&
           (!browser_ || browser_->GetHost()->TryCloseBrowser());
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    interactions_ = nullptr;
    toolbar_ = nullptr;
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    CefQuitMessageLoop();
  }

private:
  void Attach() {
    AlloyInteractions::Callbacks callbacks;
    callbacks.open_markdown = [this](CefRefPtr<CefBrowser> browser) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier())
        return false;
      ++open_count_;
      return true;
    };
    callbacks.navigate = [this](CefRefPtr<CefBrowser> browser,
                                const std::string &url) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier())
        return false;
      destinations_.push_back(url);
      return true;
    };
    callbacks.drag_enter = [this](CefRefPtr<CefBrowser>,
                                  CefRefPtr<CefDragData> data,
                                  CefDragHandler::DragOperationsMask) {
      std::vector<CefString> files;
      data->GetFileNames(files);
      const bool accepted = files.size() == 1 &&
                            files[0].ToString().size() >= 3 &&
                            files[0].ToString().substr(
                                files[0].ToString().size() - 3) == ".md";
      if (accepted)
        ++accepted_drags_;
      return accepted;
    };
    callbacks.augment_context_menu =
        [this](CefRefPtr<CefBrowser>, CefRefPtr<CefContextMenuParams> params,
               CefRefPtr<CefMenuModel> model) {
          if (!params || !model)
            return false;
          ++context_augments_;
          model->AddItem(kContextCommandId, "fixture-context-command");
          return true;
        };
    callbacks.context_menu_command =
        [this](CefRefPtr<CefBrowser>, int command_id) {
          if (command_id != kContextCommandId)
            return false;
          ++context_commands_;
          return true;
        };
    callbacks.cancel_transient = [this] { ++cancel_count_; };
    interactions_ = new AlloyInteractions(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        std::move(callbacks));
    attached_ = interactions_->Attach(window_, view_, browser_, toolbar_);
  }

  void Schedule() {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&Probe::Check, CefRefPtr<Probe>(this)),
        kPollMilliseconds);
  }
  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (!attached_) {
      if (!window_ || !browser_ || !loaded_) {
        Schedule();
        return;
      }
      Attach();
      if (!attached_) {
        Finish(false, "attach");
        return;
      }
    }
    if (stage_ == 0) {
      auto menu = interactions_->GetView(AlloyInteractions::kMenuButtonId);
      if (!menu || !menu->IsDrawn() ||
          menu->AsButton()->AsLabelButton()->GetText().ToString() != "菜单") {
        Finish(false, "menu-view");
        return;
      }
      menu->AsButton()->AsLabelButton()->AsMenuButton()->TriggerMenu();
      stage_ = 1;
      Schedule();
      return;
    }
    if (stage_ == 1) {
      if (!interactions_->menu_open()) {
        Schedule();
        return;
      }
      result_->menu_passed = true;
      window_->SendKeyPress(27, 0);
      stage_ = 2;
      Schedule();
      return;
    }
    if (stage_ == 2) {
      if (interactions_->menu_open()) {
        Schedule();
        return;
      }
      const bool opened = interactions_->HandleAccelerator(
          'O', static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN));
      const bool rejected = !interactions_->HandleAccelerator(
          'O', static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN |
                                               EVENTFLAG_SHIFT_DOWN));
      const bool copied = interactions_->Execute(AlloyMainCommand::kCopy);
      const bool pasted = interactions_->Execute(AlloyMainCommand::kPaste);
      const bool about = interactions_->Execute(AlloyMainCommand::kAbout);
      const bool licenses =
          interactions_->Execute(AlloyMainCommand::kLicenses);
      result_->command_passed =
          opened && rejected && copied && pasted && about && licenses &&
          open_count_ == 1 && destinations_.size() == 2 &&
          destinations_[0] ==
              crayon::browser::cef_shell::branding::kAboutBrowserUrl &&
          destinations_[1] == "chrome://credits/";
      auto markdown = CefDragData::Create();
      markdown->AddFile(L"C:\\fixture.md", "fixture.md");
      auto text = CefDragData::Create();
      text->AddFile(L"C:\\fixture.txt", "fixture.txt");
      auto multiple = CefDragData::Create();
      multiple->AddFile(L"C:\\a.md", "a.md");
      multiple->AddFile(L"C:\\b.md", "b.md");
      result_->drag_passed =
          interactions_->OnDragEnter(browser_, markdown, DRAG_OPERATION_COPY) &&
          !interactions_->OnDragEnter(browser_, text, DRAG_OPERATION_COPY) &&
          !interactions_->OnDragEnter(browser_, multiple,
                                      DRAG_OPERATION_COPY) &&
          accepted_drags_ == 1;
      CefMouseEvent event;
      event.x = 80;
      event.y = 80;
      browser_->GetHost()->SendMouseClickEvent(event, MBT_RIGHT, false, 1);
      browser_->GetHost()->SendMouseClickEvent(event, MBT_RIGHT, true, 1);
      stage_ = 3;
      Schedule();
      return;
    }
    if (stage_ == 3) {
      if (context_augments_ != 1 || !interactions_->context_menu_active()) {
        Schedule();
        return;
      }
      result_->context_menu_passed = interactions_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), nullptr, kContextCommandId,
          EVENTFLAG_NONE);
      window_->SendKeyPress(27, 0);
      stage_ = 4;
      Schedule();
      return;
    }
    if (stage_ == 4) {
      if (interactions_->context_menu_active()) {
        Schedule();
        return;
      }
      result_->context_menu_passed = result_->context_menu_passed &&
                                     context_commands_ == 1;
      const bool navigation_cleared = interactions_->OnNavigation(browser_) &&
                                      cancel_count_ == 1;
      auto menu_view = interactions_->GetView(AlloyInteractions::kMenuButtonId);
      result_->lifecycle_passed = interactions_->Shutdown() &&
                                  interactions_->Shutdown() && menu_view &&
                                  !menu_view->GetParentView() &&
                                  navigation_cleared && cancel_count_ == 2 &&
                                  !interactions_->Execute(
                                      AlloyMainCommand::kOpenMarkdown);
      Finish(result_->menu_passed && result_->command_passed &&
                 result_->drag_passed && result_->context_menu_passed &&
                 result_->lifecycle_passed,
             "complete");
    }
  }
  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    finished_ = true;
    std::cout << "alloy_interactions_windows passed=" << passed
              << " detail=" << detail << std::endl;
    if (window_)
      window_->Close();
  }

  std::shared_ptr<AlloyInteractionsProbeResult> result_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<AlloyInteractions> interactions_;
  std::vector<std::string> destinations_;
  int checks_ = 0;
  int stage_ = 0;
  int open_count_ = 0;
  int accepted_drags_ = 0;
  int context_augments_ = 0;
  int context_commands_ = 0;
  int cancel_count_ = 0;
  bool loaded_ = false;
  bool attached_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(Probe);
};

} // namespace

CefRefPtr<CefApp> CreateAlloyInteractionsProbe(
    std::shared_ptr<AlloyInteractionsProbeResult> result) {
  return new Probe(std::move(result));
}
