#include "alloy_window_coordinator_probe.h"

#include <windows.h>

#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "browser/window/alloy_window_coordinator.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_display.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyWindowCoordinator;
using crayon::browser::cef_shell::window::TabId;
using crayon::browser_engine::ContentCapability;
using crayon::browser_engine::ContentCapabilitySet;
using crayon::browser_engine::ContentPurpose;
using crayon::browser_engine::NavigationId;
using crayon::browser_engine::ProfileId;
using crayon::browser_session::DecodeSessionSnapshot;
using crayon::browser_session::EncodeSessionSnapshotV2;
using crayon::browser_session::SessionProfileSnapshot;
using crayon::browser_session::SessionWindowSnapshot;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 500;

template <typename T> T Required(std::optional<T> value) {
  if (!value)
    std::abort();
  return std::move(*value);
}

class AlloyWindowProbe final : public CefApp,
                               public CefBrowserProcessHandler,
                               public CefClient,
                               public CefLifeSpanHandler,
                               public CefLoadHandler,
                               public CefDisplayHandler,
                               public CefBrowserViewDelegate,
                               public CefWindowDelegate {
public:
  AlloyWindowProbe(std::string fixture_url,
                   std::shared_ptr<AlloyWindowCoordinatorProbeResult> result)
      : fixture_url_(std::move(fixture_url)), result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

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
    coordinator_ = std::make_unique<AlloyWindowCoordinator>(Callbacks());
    const auto profile =
        Required(ProfileId::TryCreate("alloy-window-probe"));
    if (!coordinator_->CreatePrimary("cancelled", profile, false) ||
        !coordinator_->CancelPendingWindow("cancelled") ||
        coordinator_->CancelPendingWindow("cancelled") ||
        coordinator_->window_count() != 0 ||
        !coordinator_->CreatePrimary("primary", profile) ||
        !CreateManagedWindow("primary", fixture_url_)) {
      Finish(false, "create-primary");
    }
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    if (pending_windows_.empty()) {
      Finish(false, "unexpected-window");
      return;
    }
    const std::string id = std::move(pending_windows_.front());
    pending_windows_.pop_front();
    auto &record = records_.at(id);
    record.window = window;
    if (!coordinator_->AttachWindow(id, window)) {
      Finish(false, "attach-window");
      return;
    }
    CefBoxLayoutSettings settings;
    auto layout = window->SetToBoxLayout(settings);
    auto *controller = coordinator_->controller(id);
    window->AddChildView(controller->container());
    layout->SetFlexForView(controller->container(), 1);
    window->SetTitle(id);
    window->SetSize(CefSize(id == "primary" ? 800 : 520, 480));
    window->Layout();
    window->Show();
    if (id == "primary") {
      window->Activate();
      ScheduleCheck();
    }
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    for (const auto &[id, record] : records_) {
      if (record.window && record.window->IsSame(window)) {
        return record.closing;
      }
    }
    return true;
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    std::cout << "alloy_window_coordinator_windows window-destroyed"
              << std::endl;
    for (auto found = records_.begin(); found != records_.end(); ++found) {
      if (found->second.window && found->second.window->IsSame(window)) {
        const std::string id = found->first;
        found->second.window = nullptr;
        if (!coordinator_->OnWindowClosed(id)) {
          Finish(false, "window-closed");
          return;
        }
        records_.erase(found);
        return;
      }
    }
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                        CefRefPtr<CefBrowser> browser) override {
    if (auto *extra = FindRestoredExtraByView(view)) {
      auto *controller = coordinator_->controller(extra->window_id);
      if (!controller ||
          !controller->OnBrowserCreated(
              view, browser,
              Required(ContentCapabilitySet::TryCreate(
                  1U << static_cast<unsigned>(ContentCapability::kNavigate)))) ||
          (extra->activate_on_create && !controller->Activate(extra->tab_id))) {
        Finish(false, "restored-extra-created");
        return;
      }
      extra->browser = browser;
      return;
    }
    auto *record = FindByView(view);
    auto *popup_controller = popup_id_.empty()
                                 ? nullptr
                                 : coordinator_->controller(popup_id_);
    if (!record && popup_controller && popup_controller->OwnsView(view) &&
        !duplicate_view_) {
      if (!popup_controller->OnBrowserCreated(
              view, browser,
              Required(ContentCapabilitySet::TryCreate(
                  1U << static_cast<unsigned>(ContentCapability::kNavigate))))) {
        Finish(false, "synchronous-duplicate-created");
        return;
      }
      pending_duplicate_browser_ = browser;
      return;
    }
    if (duplicate_view_ && duplicate_view_->IsSame(view)) {
      auto *controller = coordinator_->controller(popup_id_);
      if (!controller ||
          !controller->OnBrowserCreated(
              view, browser,
              Required(ContentCapabilitySet::TryCreate(
                  1U << static_cast<unsigned>(ContentCapability::kNavigate)))) ||
          !controller->Activate(duplicate_tab_)) {
        Finish(false, "duplicate-browser-created");
        return;
      }
      duplicate_browser_ = browser;
      return;
    }
    const bool registered =
        record && coordinator_->controller(record->id)->OnBrowserCreated(
                      view, browser,
                      Required(ContentCapabilitySet::TryCreate(
                          1U << static_cast<unsigned>(
                                    ContentCapability::kNavigate))));
    const bool activated = registered &&
                           (!record->activate_on_create ||
                            coordinator_->controller(record->id)->Activate(
                                record->tab_id));
    if (!registered || !activated) {
      std::cout << "alloy_window_coordinator_windows browser-created record="
                << static_cast<bool>(record)
                << " registered=" << registered
                << " activated=" << activated << std::endl;
      Finish(false, "browser-created");
      return;
    }
    record->browser = browser;
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> view,
                          CefRefPtr<CefBrowser>) override {
    if (auto *extra = FindRestoredExtraByView(view)) {
      extra->view = nullptr;
      extra->browser = nullptr;
      return;
    }
    if (duplicate_view_ && duplicate_view_->IsSame(view)) {
      duplicate_view_ = nullptr;
      duplicate_browser_ = nullptr;
      return;
    }
    auto *record = FindByView(view);
    if (record) {
      record->view = nullptr;
      record->browser = nullptr;
    }
  }

  bool DoClose(CefRefPtr<CefBrowser> browser) override {
    const std::string owner_id = OwnerOfBrowser(browser);
    if (owner_id.empty())
      return false;
    const bool handled = coordinator_->controller(owner_id)->OnDoClose(browser);
    std::cout << "alloy_window_coordinator_windows do-close id="
              << owner_id << " handled=" << handled << std::endl;
    if (handled) {
      CefPostTask(TID_UI,
                  base::BindOnce(&AlloyWindowProbe::ReleaseClosingView,
                                 CefRefPtr<AlloyWindowProbe>(this), browser));
    }
    return handled;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    const std::string owner_id = OwnerOfBrowser(browser);
    auto *record = FindWindow(owner_id);
    std::cout << "alloy_window_coordinator_windows before-close id="
              << (owner_id.empty() ? "missing" : owner_id) << std::endl;
    if (owner_id.empty() || !record ||
        !coordinator_->controller(owner_id)->OnBeforeClose(browser)) {
      Finish(false, "before-close");
      return;
    }
    if (record->browser && record->browser->IsSame(browser)) {
      record->browser = nullptr;
    }
    if ((duplicate_browser_ && duplicate_browser_->IsSame(browser)) ||
        (pending_duplicate_browser_ &&
         pending_duplicate_browser_->IsSame(browser))) {
      duplicate_browser_ = nullptr;
    }
    for (auto &extra : restored_extra_tabs_) {
      if (extra.browser && extra.browser->IsSame(browser)) {
        extra.browser = nullptr;
      }
    }
    if (coordinator_->controller(owner_id)->pending_count() == 0 &&
        record->window) {
      record->window->Close();
    }
  }

  bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int,
                     const CefString &target_url, const CefString &,
                     CefLifeSpanHandler::WindowOpenDisposition,
                     bool user_gesture, const CefPopupFeatures &,
                     CefWindowInfo &, CefRefPtr<CefClient> &,
                     CefBrowserSettings &, CefRefPtr<CefDictionaryValue> &,
                     bool *) override {
    auto *record = FindByBrowser(browser);
    if (!record)
      return true;
    const auto request = coordinator_->RequestPopup(
        record->id, browser, target_url.ToString(), user_gesture);
    std::cout << "alloy_window_coordinator_windows popup gesture="
              << user_gesture << " accepted=" << request.has_value()
              << std::endl;
    if (user_gesture) {
      user_popup_accepted_ = request.has_value();
      if (request)
        popup_id_ = request->window_id;
    } else {
      programmatic_denied_ = !request.has_value();
    }
    return true;
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int) override {
    if (!frame->IsMain())
      return;
    if ((duplicate_browser_ && duplicate_browser_->IsSame(browser)) ||
        (pending_duplicate_browser_ &&
         pending_duplicate_browser_->IsSame(browser))) {
      duplicate_loaded_ = true;
      return;
    }
    for (auto &extra : restored_extra_tabs_) {
      if (extra.browser && extra.browser->IsSame(browser)) {
        extra.loaded = true;
        auto *controller = coordinator_->controller(extra.window_id);
        if (!controller || !controller->SynchronizeRuntimeState(browser)) {
          Finish(false, "synchronize-extra-runtime-state");
        }
        return;
      }
    }
    auto *record = FindByBrowser(browser);
    if (record) {
      record->loaded = true;
      auto *controller = coordinator_->controller(record->id);
      if (!controller || !controller->SynchronizeRuntimeState(browser)) {
        Finish(false, "synchronize-runtime-state");
      }
    }
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString &title) override {
    if (!popup_id_.empty()) {
      auto popup = records_.find(popup_id_);
      if (popup != records_.end() && popup->second.browser &&
          popup->second.browser->IsSame(browser)) {
        transfer_ready_ = title == "transfer-ready";
        transfer_preserved_ = title == "transfer-preserved";
      }
    }
  }

private:
  struct Record final {
    std::string id;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<CefWindow> window;
    TabId tab_id = 0;
    bool loaded = false;
    bool closing = false;
    bool activate_on_create = true;
  };

  struct RestoredExtraTab final {
    std::string window_id;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefBrowser> browser;
    TabId tab_id = 0;
    bool loaded = false;
    bool activate_on_create = false;
  };

  bool CreateManagedWindow(const std::string &id, const std::string &url) {
    if (records_.count(id) != 0)
      return false;
    auto *controller = coordinator_->controller(id);
    if (!controller)
      return false;
    CefBrowserSettings settings;
    auto view = CefBrowserView::CreateBrowserView(this, url, settings, nullptr,
                                                  nullptr, this);
    const auto tab_id =
        view ? controller->BeginCreate(view, ContentPurpose::kWeb,
                                       NavigationId::FromRaw(1))
             : std::nullopt;
    if (!tab_id) {
      return false;
    }
    records_.emplace(id, Record{id, view, nullptr, nullptr, *tab_id});
    pending_windows_.push_back(id);
    CefWindow::CreateTopLevelWindow(this);
    return true;
  }

  AlloyWindowCoordinator::Callbacks Callbacks() {
    return AlloyWindowCoordinator::Callbacks{
        [this](const AlloyWindowCoordinator::PopupRequest &request) {
          if (request.url == "https://example.test/reject-create") {
            return false;
          }
          return CreateManagedWindow(request.window_id, request.url);
        },
        [this](const std::string &id) { focused_id_ = id; },
        [this](const SessionWindowSnapshot &window) {
          return CreateRestoredWindow(window);
        }};
  }

  bool CreateRestoredWindow(const SessionWindowSnapshot &snapshot) {
    if (snapshot.tabs.empty() || records_.count(snapshot.window_id) != 0) {
      return false;
    }
    auto *controller = coordinator_->controller(snapshot.window_id);
    if (!controller) {
      return false;
    }
    CefBrowserSettings settings;
    auto view = CefBrowserView::CreateBrowserView(this, snapshot.tabs.front().url,
                                                  settings, nullptr, nullptr,
                                                  this);
    const auto tab_id =
        view ? controller->BeginRestore(view, NavigationId::FromRaw(1),
                                        snapshot.tabs.front())
             : std::nullopt;
    if (!tab_id) {
      return false;
    }
    records_.emplace(snapshot.window_id,
                     Record{snapshot.window_id, view, nullptr, nullptr,
                            *tab_id, false, false,
                            snapshot.active_index == 0});
    for (std::size_t index = 1; index < snapshot.tabs.size(); ++index) {
      auto extra_view = CefBrowserView::CreateBrowserView(
          this, snapshot.tabs[index].url, settings, nullptr, nullptr, this);
      const auto extra_tab =
          extra_view
              ? controller->BeginRestore(
                    extra_view, NavigationId::FromRaw(index + 1),
                    snapshot.tabs[index])
              : std::nullopt;
      if (!extra_tab) {
        return false;
      }
      restored_extra_tabs_.push_back(
          {snapshot.window_id, extra_view, nullptr, *extra_tab, false,
           snapshot.active_index == index});
    }
    pending_windows_.push_back(snapshot.window_id);
    CefWindow::CreateTopLevelWindow(this);
    return true;
  }

  Record *FindByView(CefRefPtr<CefBrowserView> view) {
    for (auto &[id, record] : records_) {
      static_cast<void>(id);
      if (record.view && record.view->IsSame(view))
        return &record;
    }
    return nullptr;
  }

  Record *FindByBrowser(CefRefPtr<CefBrowser> browser) {
    for (auto &[id, record] : records_) {
      static_cast<void>(id);
      if (record.browser && record.browser->IsSame(browser))
        return &record;
    }
    return nullptr;
  }

  RestoredExtraTab *FindRestoredExtraByView(CefRefPtr<CefBrowserView> view) {
    for (auto &extra : restored_extra_tabs_) {
      if (extra.view && extra.view->IsSame(view)) {
        return &extra;
      }
    }
    return nullptr;
  }

  Record *FindWindow(const std::string &id) {
    auto found = records_.find(id);
    return found == records_.end() ? nullptr : &found->second;
  }

  std::string OwnerOfBrowser(CefRefPtr<CefBrowser> browser) {
    if (!browser || !coordinator_) {
      return {};
    }
    for (auto &[id, record] : records_) {
      static_cast<void>(record);
      auto *controller = coordinator_->controller(id);
      if (controller && controller->OwnsBrowser(browser)) {
        return id;
      }
    }
    return {};
  }

  bool CreateDuplicateTab(const std::string &window_id, TabId source_tab,
                          const std::string &url) {
    auto *controller = coordinator_->controller(window_id);
    if (!controller || duplicate_view_ || duplicate_browser_) {
      return false;
    }
    CefBrowserSettings settings;
    auto view = CefBrowserView::CreateBrowserView(this, url, settings, nullptr,
                                                  nullptr, this);
    const auto tab =
        view ? controller->BeginCreate(view, ContentPurpose::kWeb,
                                       NavigationId::FromRaw(2))
             : std::nullopt;
    if (!tab) {
      return false;
    }
    duplicate_source_tab_ = source_tab;
    duplicate_tab_ = *tab;
    duplicate_view_ = view;
    if (pending_duplicate_browser_) {
      duplicate_browser_ = std::move(pending_duplicate_browser_);
      if (!controller->Activate(duplicate_tab_)) {
        return false;
      }
    }
    return true;
  }

  bool ClickPrimary() {
    auto found = records_.find("primary");
    if (found == records_.end() || !found->second.view ||
        !found->second.browser) {
      return false;
    }
    found->second.view->RequestFocus();
    found->second.browser->GetHost()->SetFocus(true);
    const CefRect bounds = found->second.view->GetBounds();
    std::cout << "alloy_window_coordinator_windows click bounds="
              << bounds.width << "x" << bounds.height
              << " drawn=" << found->second.view->IsDrawn() << std::endl;
    CefMouseEvent event;
    event.x = bounds.width / 2;
    event.y = bounds.height / 2;
    found->second.browser->GetHost()->SendMouseClickEvent(
        event, MBT_LEFT, false, 1);
    found->second.browser->GetHost()->SendMouseClickEvent(event, MBT_LEFT, true,
                                                          1);
    return true;
  }

  void ReleaseClosingView(CefRefPtr<CefBrowser> browser) {
    const std::string owner_id = OwnerOfBrowser(browser);
    auto *record = FindWindow(owner_id);
    std::cout << "alloy_window_coordinator_windows release-view id="
              << (record ? record->id : "missing") << std::endl;
    if (owner_id.empty() || !record ||
        !coordinator_->controller(owner_id)->ReleaseAfterDoClose(browser)) {
      Finish(false, "release-view");
      return;
    }
    if (duplicate_browser_ && duplicate_browser_->IsSame(browser)) {
      duplicate_view_ = nullptr;
    } else {
      bool released_extra = false;
      for (auto &extra : restored_extra_tabs_) {
        if (extra.browser && extra.browser->IsSame(browser)) {
          extra.view = nullptr;
          released_extra = true;
          break;
        }
      }
      if (!released_extra) {
        record->view = nullptr;
      }
    }
  }

  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&AlloyWindowProbe::Check,
                                      CefRefPtr<AlloyWindowProbe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_)
      return;
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (logged_stage_ != stage_) {
      logged_stage_ = stage_;
      std::cout << "alloy_window_coordinator_windows stage=" << stage_
                << std::endl;
    }
    auto primary = records_.find("primary");
    if (stage_ == 0) {
      if (primary == records_.end() || !primary->second.loaded) {
        ScheduleCheck();
        return;
      }
      const bool direct_programmatic_denied =
          !coordinator_->RequestPopup("primary", primary->second.browser,
                                      "https://example.test/programmatic",
                                      false);
      const bool credentials_denied =
          !coordinator_->RequestPopup("primary", primary->second.browser,
                                      "https://user:pass@example.test/", true);
      const bool failed_creation_rolled_back =
          !coordinator_->RequestPopup("primary", primary->second.browser,
                                      "https://example.test/reject-create",
                                      true) &&
          coordinator_->window_count() == 1;
      result_->policy_passed = direct_programmatic_denied &&
                               credentials_denied &&
                               failed_creation_rolled_back;
      std::cout << "alloy_window_coordinator_windows policy programmatic="
                << direct_programmatic_denied
                << " credentials=" << credentials_denied
                << " rollback=" << failed_creation_rolled_back << std::endl;
      if (!result_->policy_passed || !ClickPrimary()) {
        Finish(false, "policy-or-click");
        return;
      }
      last_click_check_ = checks_;
      ++stage_;
    } else if (stage_ == 1) {
      auto popup = records_.find(popup_id_);
      if (!user_popup_accepted_ || popup == records_.end() ||
          !popup->second.loaded) {
        if (!user_popup_accepted_ && checks_ - last_click_check_ >= 10) {
          if (!ClickPrimary()) {
            Finish(false, "retry-click");
            return;
          }
          last_click_check_ = checks_;
        }
        ScheduleCheck();
        return;
      }
      result_->real_popup_passed =
          coordinator_->is_popup(popup_id_) &&
          coordinator_->opener_of(popup_id_) ==
              std::optional<std::string>("primary") &&
          popup->second.browser && primary->second.browser &&
          !popup->second.browser->IsSame(primary->second.browser);
      const auto popup_tab =
          coordinator_->controller(popup_id_)->model().active_tab();
      if (!result_->real_popup_passed || !popup_tab ||
          !coordinator_->controller(popup_id_)->PinTab(*popup_tab, true) ||
          !coordinator_->controller(popup_id_)->MuteTab(*popup_tab, true) ||
          !coordinator_->controller(popup_id_)
               ->SetTabGroup(*popup_tab, std::string("group-a"))) {
        Finish(false, "popup-or-advanced-state");
        return;
      }
      popup_tab_before_move_ = *popup_tab;
      popup_browser_id_before_move_ = popup->second.browser->GetIdentifier();
      if (!CreateDuplicateTab(popup_id_, *popup_tab,
                              popup->second.browser->GetMainFrame()->GetURL())) {
        Finish(false, "create-duplicate");
        return;
      }
      ++stage_;
    } else if (stage_ == 2) {
      if (!duplicate_loaded_ || !duplicate_browser_) {
        ScheduleCheck();
        return;
      }
      auto *popup_controller = coordinator_->controller(popup_id_);
      duplicate_passed_ =
          popup_controller->CopyAdvancedState(duplicate_source_tab_,
                                              duplicate_tab_) &&
          duplicate_browser_->GetIdentifier() !=
              popup_browser_id_before_move_ &&
          popup_controller->IsPinned(duplicate_tab_) &&
          popup_controller->IsMuted(duplicate_tab_) &&
          popup_controller->TabGroup(duplicate_tab_) ==
              std::optional<std::string>("group-a") &&
          duplicate_browser_->GetHost()->IsAudioMuted();
      if (!duplicate_passed_ ||
          !popup_controller->RequestClose(duplicate_tab_, true)) {
        Finish(false, "duplicate-state-or-close");
        return;
      }
      ++stage_;
    } else if (stage_ == 3) {
      if (duplicate_browser_) {
        ScheduleCheck();
        return;
      }
      auto popup = records_.find(popup_id_);
      if (popup == records_.end() ||
          !coordinator_->controller(popup_id_)->Activate(
              popup_tab_before_move_)) {
        Finish(false, "reactivate-source");
        return;
      }
      popup->second.browser->GetMainFrame()->ExecuteJavaScript(
          "document.body.dataset.transfer='kept';"
          "document.title='transfer-ready';",
          popup->second.browser->GetMainFrame()->GetURL(), 1);
      ++stage_;
    } else if (stage_ == 4) {
      if (!transfer_ready_) {
        ScheduleCheck();
        return;
      }
      auto popup = records_.find(popup_id_);
      const auto moved = coordinator_->MoveTab(
          popup_id_, popup_tab_before_move_, "primary");
      const bool target_owned =
          moved && coordinator_->controller("primary")->OwnsBrowser(
                       popup->second.browser) &&
          !coordinator_->controller(popup_id_)->OwnsBrowser(
              popup->second.browser);
      const auto returned =
          moved ? coordinator_->MoveTab("primary", *moved, popup_id_)
                : std::nullopt;
      result_->advanced_transfer_passed =
          target_owned && returned &&
          coordinator_->controller(popup_id_)->OwnsBrowser(
              popup->second.browser) &&
          popup->second.browser->GetIdentifier() ==
              popup_browser_id_before_move_ &&
          coordinator_->controller(popup_id_)->IsPinned(*returned) &&
          coordinator_->controller(popup_id_)->IsMuted(*returned) &&
          coordinator_->controller(popup_id_)->TabGroup(*returned) ==
              std::optional<std::string>("group-a") &&
          popup->second.browser->GetHost()->IsAudioMuted();
      if (!result_->advanced_transfer_passed) {
        Finish(false, "advanced-transfer");
        return;
      }
      popup->second.browser->GetMainFrame()->ExecuteJavaScript(
          "document.title=document.body.dataset.transfer==='kept'?"
          "'transfer-preserved':'transfer-lost';",
          popup->second.browser->GetMainFrame()->GetURL(), 1);
      ++stage_;
    } else if (stage_ == 5) {
      if (!transfer_preserved_) {
        ScheduleCheck();
        return;
      }
      const auto snapshot = coordinator_->SnapshotSession(
          Required(ProfileId::TryCreate("alloy-window-probe")));
      auto restore_input = snapshot;
      if (restore_input) {
        for (auto &window : restore_input->windows) {
          if (window.window_id == "primary") {
            window.tabs.push_back({fixture_url_, false, false,
                                   std::string("group-restored")});
            window.active_index = 1;
          }
        }
      }
      const auto encoded = restore_input ? EncodeSessionSnapshotV2(*restore_input)
                                         : std::nullopt;
      restored_snapshot_ = encoded ? DecodeSessionSnapshot(*encoded)
                                   : std::nullopt;
      if (!restored_snapshot_ || restored_snapshot_->windows.size() != 2) {
        Finish(false, "session-snapshot");
        return;
      }
      primary->second.closing = true;
      if (!coordinator_->BeginCloseWindow("primary", true)) {
        Finish(false, "close-opener");
        return;
      }
      ++stage_;
    } else if (stage_ == 6) {
      if (coordinator_->has_window("primary")) {
        ScheduleCheck();
        return;
      }
      auto popup = records_.find(popup_id_);
      result_->isolation_passed = coordinator_->has_window(popup_id_) &&
                                  popup != records_.end() &&
                                  popup->second.browser &&
                                  focused_id_ == popup_id_;
      if (!result_->isolation_passed) {
        Finish(false, "opener-isolation");
        return;
      }
      popup->second.closing = true;
      if (!coordinator_->BeginCloseWindow(popup_id_, true)) {
        Finish(false, "close-popup");
        return;
      }
      ++stage_;
    } else if (stage_ == 7) {
      if (coordinator_->window_count() != 0) {
        ScheduleCheck();
        return;
      }
      if (!coordinator_->Shutdown()) {
        Finish(false, "shutdown-before-restore");
        return;
      }
      coordinator_ = std::make_unique<AlloyWindowCoordinator>(Callbacks());
      const auto profile = Required(ProfileId::TryCreate("alloy-window-probe"));
      if (!restored_snapshot_ ||
          !coordinator_->RestoreSession(*restored_snapshot_, profile) ||
          coordinator_->RestoreSession(*restored_snapshot_, profile)) {
        Finish(false, "restore-session");
        return;
      }
      ++stage_;
    } else if (stage_ == 8) {
      if (records_.size() != 2) {
        ScheduleCheck();
        return;
      }
      for (const auto &[id, record] : records_) {
        static_cast<void>(id);
        if (!record.loaded || !record.browser) {
          ScheduleCheck();
          return;
        }
      }
      for (const auto &extra : restored_extra_tabs_) {
        if (!extra.loaded || !extra.browser) {
          ScheduleCheck();
          return;
        }
      }
      auto restored_popup = records_.find(popup_id_);
      auto *restored_controller = coordinator_->controller(popup_id_);
      const bool restored_pinned =
          restored_popup != records_.end() && restored_controller &&
          restored_controller->IsPinned(restored_popup->second.tab_id);
      const bool restored_muted =
          restored_popup != records_.end() && restored_controller &&
          restored_controller->IsMuted(restored_popup->second.tab_id);
      const bool restored_group =
          restored_popup != records_.end() && restored_controller &&
          restored_controller->TabGroup(restored_popup->second.tab_id) ==
              std::optional<std::string>("group-a");
      const bool restored_audio =
          restored_popup != records_.end() && restored_popup->second.browser &&
          restored_popup->second.browser->GetHost()->IsAudioMuted();
      std::cout << "alloy_window_coordinator_windows restored pinned="
                << restored_pinned << " muted=" << restored_muted
                << " group=" << restored_group << " audio=" << restored_audio
                << std::endl;
      result_->session_restore_passed =
          restored_pinned && restored_muted && restored_group && restored_audio;
      const auto roundtrip = coordinator_->SnapshotSession(
          Required(ProfileId::TryCreate("alloy-window-probe")));
      bool multi_tab_roundtrip = false;
      if (roundtrip) {
        for (const auto &window : roundtrip->windows) {
          if (window.window_id == "primary") {
            multi_tab_roundtrip =
                window.tabs.size() == 2 && window.active_index == 1 &&
                window.tabs[1].group ==
                    std::optional<std::string>("group-restored");
          }
        }
      }
      result_->session_restore_passed =
          result_->session_restore_passed && multi_tab_roundtrip;
      if (!result_->session_restore_passed) {
        Finish(false, "restored-state");
        return;
      }
      for (auto &[id, record] : records_) {
        record.closing = true;
        if (!coordinator_->BeginCloseWindow(id, true)) {
          Finish(false, "close-restored");
          return;
        }
      }
      ++stage_;
    } else if (stage_ == 9) {
      if (coordinator_->window_count() != 0) {
        ScheduleCheck();
        return;
      }
      result_->windows_closed = coordinator_->Shutdown();
      Finish(result_->windows_closed, "complete");
      return;
    }
    ScheduleCheck();
  }

  void Finish(bool passed, const char *detail) {
    if (finished_)
      return;
    passed_ = passed;
    finished_ = true;
    result_->behavior_passed = passed_ && result_->real_popup_passed &&
                               result_->policy_passed &&
                               result_->isolation_passed &&
                               result_->advanced_transfer_passed &&
                               result_->session_restore_passed &&
                               duplicate_passed_;
    std::cout << "alloy_window_coordinator_windows detail=" << detail
              << " passed=" << result_->behavior_passed << std::endl;
    if (records_.empty()) {
      CefQuitMessageLoop();
      return;
    }
    for (auto &[id, record] : records_) {
      record.closing = true;
      static_cast<void>(coordinator_->BeginCloseWindow(id, true));
    }
  }

  const std::string fixture_url_;
  std::shared_ptr<AlloyWindowCoordinatorProbeResult> result_;
  std::unique_ptr<AlloyWindowCoordinator> coordinator_;
  std::map<std::string, Record> records_;
  std::vector<RestoredExtraTab> restored_extra_tabs_;
  std::deque<std::string> pending_windows_;
  std::string popup_id_;
  std::string focused_id_;
  TabId popup_tab_before_move_ = 0;
  TabId duplicate_source_tab_ = 0;
  TabId duplicate_tab_ = 0;
  int popup_browser_id_before_move_ = 0;
  CefRefPtr<CefBrowserView> duplicate_view_;
  CefRefPtr<CefBrowser> duplicate_browser_;
  CefRefPtr<CefBrowser> pending_duplicate_browser_;
  int stage_ = 0;
  int logged_stage_ = -1;
  int checks_ = 0;
  int last_click_check_ = 0;
  bool programmatic_denied_ = false;
  bool user_popup_accepted_ = false;
  bool transfer_ready_ = false;
  bool transfer_preserved_ = false;
  bool duplicate_loaded_ = false;
  bool duplicate_passed_ = false;
  std::optional<SessionProfileSnapshot> restored_snapshot_;
  bool passed_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(AlloyWindowProbe);
};

} // namespace

CefRefPtr<CefApp> CreateAlloyWindowCoordinatorProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyWindowCoordinatorProbeResult> result) {
  return new AlloyWindowProbe(std::move(fixture_url), std::move(result));
}
