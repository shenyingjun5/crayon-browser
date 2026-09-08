#include "alloy_interactions_probe.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "browser/branding/about_destination.h"
#include "browser/window/alloy_activity_surface.h"
#include "browser/window/alloy_interactions.h"
#include "browser/window/alloy_tab_transfer_surface.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_menu_model.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyActivitySurface;
using crayon::browser::cef_shell::window::AlloyDownloads;
using crayon::browser::cef_shell::window::AlloyHistory;
using crayon::browser::cef_shell::window::AlloyInteractions;
using crayon::browser::cef_shell::window::AlloyMainCommand;
using crayon::browser::cef_shell::window::AlloyTabTransferSurface;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 400;
constexpr int kContextCommandId = MENU_ID_USER_FIRST + 77;

class TestContextMenuParams final : public CefContextMenuParams {
public:
  TestContextMenuParams() = default;

  int GetXCoord() override { return 80; }
  int GetYCoord() override { return 80; }
  TypeFlags GetTypeFlags() override { return CM_TYPEFLAG_PAGE; }
  CefString GetLinkUrl() override { return {}; }
  CefString GetUnfilteredLinkUrl() override { return {}; }
  CefString GetSourceUrl() override { return {}; }
  bool HasImageContents() override { return false; }
  CefString GetTitleText() override { return {}; }
  CefString GetPageUrl() override { return "about:blank"; }
  CefString GetFrameUrl() override { return "about:blank"; }
  CefString GetFrameCharset() override { return "UTF-8"; }
  MediaType GetMediaType() override { return CM_MEDIATYPE_NONE; }
  MediaStateFlags GetMediaStateFlags() override { return CM_MEDIAFLAG_NONE; }
  CefString GetSelectionText() override { return {}; }
  CefString GetMisspelledWord() override { return {}; }
  bool GetDictionarySuggestions(std::vector<CefString> &) override {
    return false;
  }
  bool IsEditable() override { return false; }
  bool IsSpellCheckEnabled() override { return false; }
  EditStateFlags GetEditStateFlags() override { return CM_EDITFLAG_NONE; }
  bool IsCustomMenu() override { return false; }

private:
  IMPLEMENT_REFCOUNTING(TestContextMenuParams);
  DISALLOW_COPY_AND_ASSIGN(TestContextMenuParams);
};

class TestMenuModelDelegate final : public CefMenuModelDelegate {
public:
  TestMenuModelDelegate() = default;
  void ExecuteCommand(CefRefPtr<CefMenuModel>, int,
                      cef_event_flags_t) override {}

private:
  IMPLEMENT_REFCOUNTING(TestMenuModelDelegate);
  DISALLOW_COPY_AND_ASSIGN(TestMenuModelDelegate);
};

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
  void
  OnBeforeCommandLineProcessing(const CefString &,
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
    activity_toolbar_ = CefPanel::CreatePanel(nullptr);
    transfer_toolbar_ = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings row;
    row.horizontal = true;
    toolbar_->SetToBoxLayout(row);
    activity_toolbar_->SetToBoxLayout(row);
    transfer_toolbar_->SetToBoxLayout(row);
    window_->AddChildView(toolbar_);
    window_->AddChildView(activity_toolbar_);
    window_->AddChildView(transfer_toolbar_);
    window_->AddChildView(view_);
    window_layout->SetFlexForView(view_, 1);
    window_->SetSize(CefSize(720, 480));
    window_->Layout();
    window_->Show();
  }
  bool OnAccelerator(CefRefPtr<CefWindow>, int command_id) override {
    return interactions_ && interactions_->HandleAccelerator(
                                command_id, static_cast<cef_event_flags_t>(
                                                EVENTFLAG_CONTROL_DOWN));
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    return finished_ && (!browser_ || browser_->GetHost()->TryCloseBrowser());
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    activity_ = nullptr;
    transfer_ = nullptr;
    downloads_.reset();
    history_.reset();
    interactions_ = nullptr;
    toolbar_ = nullptr;
    activity_toolbar_ = nullptr;
    transfer_toolbar_ = nullptr;
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    CefQuitMessageLoop();
  }

private:
  void Attach() {
    auto profile = crayon::browser_engine::ProfileId::TryCreate("probe");
    if (!profile)
      return;
    history_ = std::make_unique<AlloyHistory>(
        *profile, false,
        AlloyHistory::Callbacks{[this](const std::string &url) {
          restored_url_ = url;
          return true;
        }});
    history_->BeginNavigation(1);
    history_->CommitNavigation(1, "https://history.test/", "Fixture history",
                               10);
    history_->RecordClosedTab("https://closed.test/", "Closed fixture", 20);

    AlloyDownloads::Callbacks download_callbacks;
    download_callbacks.confirm_pending = [this](std::uint64_t,
                                                const std::string &) {
      ++kept_downloads_;
      return true;
    };
    download_callbacks.discard_pending = [](std::uint64_t) { return true; };
    download_callbacks.pause = [this](std::uint64_t) {
      ++paused_downloads_;
      return true;
    };
    download_callbacks.resume = [](std::uint64_t) { return true; };
    download_callbacks.cancel = [](std::uint64_t) { return true; };
    download_callbacks.open_location = [](const std::string &) { return true; };
    downloads_ = std::make_unique<AlloyDownloads>(
        "C:/verified-downloads", [](const std::string &) { return false; },
        std::move(download_callbacks));
    downloads_->OnDownloadStarting(11, "danger.exe",
                                   "https://download.test/danger.exe");
    downloads_->OnDownloadStarting(12, "safe.txt",
                                   "https://download.test/safe.txt");

    AlloyActivitySurface::Callbacks activity_callbacks;
    activity_callbacks.navigate_current = [](const std::string &) {
      return true;
    };
    activity_callbacks.confirm_clear_history = [this] {
      ++clear_confirmations_;
      return allow_history_clear_;
    };
    activity_callbacks.persist_history = [this] {
      ++history_persists_;
      return allow_history_persist_;
    };
    activity_callbacks.confirm_dangerous = [](const std::string &name) {
      return name == "danger.exe";
    };
    activity_ = new AlloyActivitySurface(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        history_.get(), downloads_.get(), std::move(activity_callbacks));
    if (!activity_->Attach(window_, activity_toolbar_))
      return;
    transfer_ = new AlloyTabTransferSurface(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        AlloyTabTransferSurface::Callbacks{
            [] {
              return std::vector<
                  crayon::browser::cef_shell::window::AlloyTabTransferTarget>{
                  {"popup-1", "窗口 1"}};
            },
            [this](const std::string &target) {
              if (target != "popup-1")
                return false;
              ++transfer_commands_;
              return true;
            }});
    if (!transfer_->Attach(window_, transfer_toolbar_))
      return;

    AlloyInteractions::Callbacks callbacks;
    callbacks.open_markdown = [this](CefRefPtr<CefBrowser> browser) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier())
        return false;
      ++open_count_;
      return true;
    };
    callbacks.open_incognito = [this] {
      ++incognito_count_;
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
      const bool accepted =
          files.size() == 1 && files[0].ToString().size() >= 3 &&
          files[0].ToString().substr(files[0].ToString().size() - 3) == ".md";
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
    callbacks.context_menu_command = [this](CefRefPtr<CefBrowser>,
                                            int command_id) {
      if (command_id != kContextCommandId)
        return false;
      ++context_commands_;
      return true;
    };
    callbacks.daily_state = [this] {
      ++daily_state_reads_;
      return crayon::browser::cef_shell::window::AlloyDailyCommandState{
          true, true, true, true, true};
    };
    callbacks.daily_command = [this](AlloyMainCommand) {
      ++daily_commands_;
      return true;
    };
    callbacks.tab_search_entries = [] {
      return std::vector<
          crayon::browser::cef_shell::window::AlloyTabSearchEntry>{
          {0, "Invalid tab", false},
          {11, "Fixture tab", false},
          {12, "Active tab", true}};
    };
    callbacks.activate_searched_tab = [this](std::uint64_t tab_id) {
      activated_search_tab_ = tab_id;
      return tab_id == 11;
    };
    callbacks.bookmark_state = [] {
      return crayon::browser::cef_shell::window::AlloyBookmarkCommandState{
          true,
          true,
          true,
          {{0, "Invalid bookmark"}, {41, "Fixture bookmark"}}};
    };
    callbacks.toggle_current_bookmark = [this] {
      ++bookmark_toggles_;
      return true;
    };
    callbacks.open_bookmark = [this](std::uint64_t node_id) {
      opened_bookmark_ = node_id;
      return node_id == 41;
    };
    callbacks.cancel_transient = [this] { ++cancel_count_; };
    interactions_ = new AlloyInteractions(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        std::move(callbacks));
    attached_ = interactions_->Attach(window_, view_, browser_, toolbar_);
  }

  void Schedule() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&Probe::Check, CefRefPtr<Probe>(this)),
                       kPollMilliseconds);
  }
  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      std::cout << "alloy_interactions_timeout stage=" << stage_
                << " menu_open="
                << (interactions_ && interactions_->menu_open())
                << " context_augments=" << context_augments_
                << " context_active="
                << (interactions_ && interactions_->context_menu_active())
                << std::endl;
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
      CefRefPtr<CefContextMenuParams> params = new TestContextMenuParams();
      auto context_menu =
          CefMenuModel::CreateMenuModel(new TestMenuModelDelegate());
      interactions_->OnBeforeContextMenu(browser_, browser_->GetMainFrame(),
                                         params, context_menu);
      const bool command = interactions_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), params, kContextCommandId,
          EVENTFLAG_NONE);
      interactions_->OnContextMenuDismissed(browser_, browser_->GetMainFrame());
      const int context_index =
          context_menu ? context_menu->GetIndexOf(kContextCommandId) : -1;
      result_->context_menu_passed = context_menu && context_augments_ == 1 &&
                                     context_index >= 0 && command &&
                                     context_commands_ == 1 &&
                                     !interactions_->context_menu_active();
      auto menu = interactions_->GetView(AlloyInteractions::kMenuButtonId);
      const auto menu_label = menu && menu->AsButton()
                                  ? menu->AsButton()->AsLabelButton()
                                  : nullptr;
      if (!menu || !menu->IsDrawn() || !menu_label ||
          !menu_label->GetText().empty() ||
          !menu_label->GetImage(CEF_BUTTON_STATE_NORMAL)) {
        Finish(false, "menu-view");
        return;
      }
      const auto bookmark_toggle_view =
          interactions_->GetView(AlloyInteractions::kBookmarkButtonId);
      const auto bookmark_toggle =
          bookmark_toggle_view ? bookmark_toggle_view->AsButton() : nullptr;
      const auto bookmark_label =
          bookmark_toggle ? bookmark_toggle->AsLabelButton() : nullptr;
      if (!bookmark_label || !bookmark_label->GetText().empty() ||
          !bookmark_label->GetImage(CEF_BUTTON_STATE_NORMAL) ||
          !bookmark_label->IsFocusable()) {
        Finish(false, "bookmark-icon");
        return;
      }
      bookmark_toggle->RequestFocus();
      if (!bookmark_toggle->HasFocus()) {
        Finish(false, "bookmark-focus-setup");
        return;
      }
      interactions_->OnButtonPressed(bookmark_toggle);
      if (bookmark_toggle->HasFocus() || !bookmark_toggle->IsFocusable()) {
        Finish(false, "bookmark-focus-release");
        return;
      }
      const auto bookmark_item_view =
          interactions_->GetView(AlloyInteractions::kBookmarkBarCommandBase);
      const auto bookmark_item =
          bookmark_item_view ? bookmark_item_view->AsButton() : nullptr;
      interactions_->OnButtonPressed(bookmark_item);
      if (bookmark_toggles_ != 1 || opened_bookmark_ != 41) {
        Finish(false, "bookmark-controls");
        return;
      }
      activity_->ExecuteCommand(nullptr, AlloyActivitySurface::kRestoreClosedId,
                                EVENTFLAG_NONE);
      activity_->ExecuteCommand(nullptr, AlloyActivitySurface::kClearHistoryId,
                                EVENTFLAG_NONE);
      allow_history_clear_ = true;
      allow_history_persist_ = false;
      activity_->ExecuteCommand(nullptr, AlloyActivitySurface::kClearHistoryId,
                                EVENTFLAG_NONE);
      const auto history_view =
          activity_->GetView(AlloyActivitySurface::kHistoryButtonId);
      const auto history_button =
          history_view
              ? history_view->AsButton()->AsLabelButton()->AsMenuButton()
              : nullptr;
      const auto downloads_view =
          activity_->GetView(AlloyActivitySurface::kDownloadsButtonId);
      const auto downloads_button =
          downloads_view
              ? downloads_view->AsButton()->AsLabelButton()->AsMenuButton()
              : nullptr;
      if (!history_button || !downloads_button ||
          !history_button->GetText().empty() ||
          !downloads_button->GetText().empty() ||
          !history_button->GetImage(CEF_BUTTON_STATE_NORMAL) ||
          !downloads_button->GetImage(CEF_BUTTON_STATE_NORMAL) ||
          restored_url_ != "https://closed.test/" || history_persists_ != 2 ||
          clear_confirmations_ != 2 ||
          history_->store().entries().size() != 1) {
        Finish(false, "activity-state");
        return;
      }
      result_->activity_passed =
          history_view->IsDrawn() && downloads_view->IsDrawn() &&
          downloads_->ConfirmDangerous(11) && downloads_->Pause(12) &&
          kept_downloads_ == 1 && paused_downloads_ == 1;
      auto transfer_button = transfer_->button();
      if (!transfer_button || !transfer_button->IsDrawn() ||
          !transfer_button->AsButton() ||
          !transfer_button->AsButton()->AsLabelButton() ||
          !transfer_button->AsButton()->AsLabelButton()->AsMenuButton() ||
          !transfer_button->AsButton()->AsLabelButton()->GetText().empty() ||
          !transfer_button->AsButton()->AsLabelButton()->GetImage(
              CEF_BUTTON_STATE_NORMAL)) {
        Finish(false, "transfer-view");
        return;
      }
      transfer_button->AsButton()
          ->AsLabelButton()
          ->AsMenuButton()
          ->TriggerMenu();
      stage_ = 1;
      Schedule();
      return;
    }
    if (stage_ == 1) {
      if (!transfer_->menu_open()) {
        Schedule();
        return;
      }
      transfer_->ExecuteCommand(
          nullptr, AlloyTabTransferSurface::kTargetCommandBase, EVENTFLAG_NONE);
      window_->SendKeyPress(27, 0);
      stage_ = 2;
      Schedule();
      return;
    }
    if (stage_ == 2) {
      if (!transfer_close_settled_) {
        transfer_close_settled_ = true;
        Schedule();
        return;
      }
      if (!transfer_->Refresh()) {
        Finish(false, "transfer-refresh");
        return;
      }
      transfer_->button()
          ->AsButton()
          ->AsLabelButton()
          ->AsMenuButton()
          ->TriggerMenu();
      stage_ = 3;
      Schedule();
      return;
    }
    if (stage_ == 3) {
      if (!transfer_->menu_open()) {
        Schedule();
        return;
      }
      transfer_->ExecuteCommand(
          nullptr, AlloyTabTransferSurface::kTargetCommandBase, EVENTFLAG_NONE);
      result_->transfer_passed = transfer_commands_ == 2;
      window_->SendKeyPress(27, 0);
      stage_ = 4;
      Schedule();
      return;
    }
    if (stage_ == 4) {
      if (!second_transfer_close_settled_) {
        second_transfer_close_settled_ = true;
        Schedule();
        return;
      }
      interactions_->GetView(AlloyInteractions::kMenuButtonId)
          ->AsButton()
          ->AsLabelButton()
          ->AsMenuButton()
          ->TriggerMenu();
      stage_ = 5;
      Schedule();
      return;
    }
    if (stage_ == 5) {
      if (!interactions_->menu_open()) {
        Schedule();
        return;
      }
      interactions_->ExecuteCommand(
          nullptr, AlloyInteractions::kTabSearchCommandBase, EVENTFLAG_NONE);
      result_->menu_passed = activated_search_tab_ == 11;
      window_->SendKeyPress(27, 0);
      stage_ = 6;
      Schedule();
      return;
    }
    if (stage_ == 6) {
      if (interactions_->menu_open()) {
        Schedule();
        return;
      }
      if (!menu_close_settled_) {
        menu_close_settled_ = true;
        Schedule();
        return;
      }
      const bool opened = interactions_->HandleAccelerator(
          'O', static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN));
      const bool rejected = !interactions_->HandleAccelerator(
          'O', static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN |
                                              EVENTFLAG_SHIFT_DOWN));
      const bool incognito = interactions_->HandleAccelerator(
          'N', static_cast<cef_event_flags_t>(EVENTFLAG_CONTROL_DOWN |
                                              EVENTFLAG_SHIFT_DOWN));
      const bool copied = interactions_->Execute(AlloyMainCommand::kCopy);
      const bool pasted = interactions_->Execute(AlloyMainCommand::kPaste);
      const bool about = interactions_->Execute(AlloyMainCommand::kAbout);
      const bool licenses = interactions_->Execute(AlloyMainCommand::kLicenses);
      const bool pinned = interactions_->Execute(AlloyMainCommand::kTogglePin);
      const bool duplicated =
          interactions_->Execute(AlloyMainCommand::kDuplicateTab);
      const bool muted = interactions_->Execute(AlloyMainCommand::kToggleMute);
      const bool grouped =
          interactions_->Execute(AlloyMainCommand::kToggleGroup);
      const bool bookmark_bar =
          interactions_->Execute(AlloyMainCommand::kToggleBookmarkBar);
      result_->command_passed =
          opened && rejected && incognito && copied && pasted && about &&
          licenses && pinned && duplicated && muted && grouped &&
          bookmark_bar && daily_state_reads_ >= 1 && daily_commands_ == 5 &&
          open_count_ == 1 && incognito_count_ == 1 &&
          destinations_.size() == 2 &&
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
      const bool navigation_cleared =
          interactions_->OnNavigation(browser_) && cancel_count_ == 1;
      auto menu_view = interactions_->GetView(AlloyInteractions::kMenuButtonId);
      auto activity_history_view =
          activity_->GetView(AlloyActivitySurface::kHistoryButtonId);
      result_->lifecycle_passed =
          activity_->Shutdown() && activity_->Shutdown() &&
          activity_history_view && !activity_history_view->GetParentView() &&
          interactions_->Shutdown() && interactions_->Shutdown() && menu_view &&
          !menu_view->GetParentView() && navigation_cleared &&
          cancel_count_ == 2 && transfer_->Shutdown() &&
          transfer_->Shutdown() &&
          !interactions_->Execute(AlloyMainCommand::kOpenMarkdown);
      Finish(result_->menu_passed && result_->command_passed &&
                 result_->drag_passed && result_->context_menu_passed &&
                 result_->activity_passed && result_->transfer_passed &&
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
  CefRefPtr<CefPanel> activity_toolbar_;
  CefRefPtr<CefPanel> transfer_toolbar_;
  CefRefPtr<AlloyInteractions> interactions_;
  CefRefPtr<AlloyActivitySurface> activity_;
  CefRefPtr<AlloyTabTransferSurface> transfer_;
  std::unique_ptr<AlloyHistory> history_;
  std::unique_ptr<AlloyDownloads> downloads_;
  std::vector<std::string> destinations_;
  int checks_ = 0;
  int stage_ = 0;
  int open_count_ = 0;
  int incognito_count_ = 0;
  int accepted_drags_ = 0;
  int context_augments_ = 0;
  int context_commands_ = 0;
  int daily_state_reads_ = 0;
  int daily_commands_ = 0;
  int bookmark_toggles_ = 0;
  std::uint64_t opened_bookmark_ = 0;
  std::uint64_t activated_search_tab_ = 0;
  int cancel_count_ = 0;
  int clear_confirmations_ = 0;
  int history_persists_ = 0;
  int transfer_commands_ = 0;
  bool transfer_close_settled_ = false;
  bool second_transfer_close_settled_ = false;
  int kept_downloads_ = 0;
  int paused_downloads_ = 0;
  std::string restored_url_;
  bool allow_history_clear_ = false;
  bool allow_history_persist_ = true;
  bool loaded_ = false;
  bool attached_ = false;
  bool menu_close_settled_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(Probe);
};

} // namespace

CefRefPtr<CefApp> CreateAlloyInteractionsProbe(
    std::shared_ptr<AlloyInteractionsProbeResult> result) {
  return new Probe(std::move(result));
}
