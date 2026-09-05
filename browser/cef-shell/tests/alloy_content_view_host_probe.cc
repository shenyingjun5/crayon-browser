#include "alloy_content_view_host_probe.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "browser/window/alloy_content_view_host.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::window::AlloyContentViewHost;
using namespace crayon::browser_engine;
using crayon::browser_shell::ContentViewRegistryResult;

constexpr int kWideWidth = 900;
constexpr int kNarrowWidth = 560;
constexpr int kWindowHeight = 600;
constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 500;
constexpr char kFirstUrl[] = "data:text/html,<title>first-ready</title>first";
constexpr char kSecondUrl[] =
    "data:text/html,<title>second-ready</title>second";

template <typename T> T Required(std::optional<T> value) {
  if (!value.has_value()) {
    std::abort();
  }
  return std::move(*value);
}

template <typename T> T Id(const char *value) {
  return Required(T::TryCreate(std::string(value)));
}

class AlloyHostProbe final : public CefApp,
                             public CefBrowserProcessHandler,
                             public CefClient,
                             public CefLifeSpanHandler,
                             public CefLoadHandler,
                             public CefDisplayHandler,
                             public CefBrowserViewDelegate,
                             public CefWindowDelegate {
public:
  explicit AlloyHostProbe(
      std::shared_ptr<AlloyContentViewHostProbeResult> result)
      : result_(std::move(result)), first_mount_(Mount("first", 1)),
        second_mount_(Mount("second", 1)) {}

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
    host_ = std::make_unique<AlloyContentViewHost>(2);
    CefBrowserSettings settings;
    views_[0] = CefBrowserView::CreateBrowserView(this, kFirstUrl, settings,
                                                  nullptr, nullptr, this);
    views_[1] = CefBrowserView::CreateBrowserView(this, kSecondUrl, settings,
                                                  nullptr, nullptr, this);
    if (!views_[0] || !views_[1] ||
        host_->Mount(first_mount_, views_[0]) !=
            ContentViewRegistryResult::kAccepted ||
        host_->Mount(second_mount_, views_[1]) !=
            ContentViewRegistryResult::kAccepted) {
      Finish(false, "mount");
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
    settings.horizontal = false;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(host_->container());
    layout->SetFlexForView(host_->container(), 1);
    window_->SetSize(CefSize(kWideWidth, kWindowHeight));
    window_->Layout();
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override {
    const int index = ViewIndex(browser_view);
    if (index < 0) {
      Finish(false, "unknown-created-view");
      return;
    }
    browsers_[index] = browser;
    browser_ids_[index] = browser->GetIdentifier();
    const auto capabilities = Required(ContentCapabilitySet::TryCreate(
        (1U << static_cast<unsigned>(ContentCapability::kNavigate)) |
        (1U << static_cast<unsigned>(ContentCapability::kZoom))));
    if (host_->OnResult(ContentViewResult{
            index == 0 ? first_mount_ : second_mount_,
            ContentViewResultKind::kCreated, capabilities,
            EngineErrorCode::kNone}) != ContentViewRegistryResult::kAccepted) {
      Finish(false, "created-result");
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int) override {
    if (!frame->IsMain()) {
      return;
    }
    const int index = BrowserIndex(browser);
    if (index >= 0) {
      loaded_[index] = true;
    }
  }

  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString &title) override {
    if (title == "first-state-set") {
      first_state_set_ = true;
    } else if (title == "state-preserved") {
      state_preserved_ = true;
    } else if (title == "state-lost") {
      Finish(false, "state-lost");
    }
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser>) override {
    const int index = ViewIndex(browser_view);
    if (index < 0) {
      return;
    }
    const auto &mount = index == 0 ? first_mount_ : second_mount_;
    if (host_->OnResult(ContentViewResult{
            mount, ContentViewResultKind::kClosed, ContentCapabilitySet::None(),
            EngineErrorCode::kNone}) != ContentViewRegistryResult::kAccepted) {
      passed_ = false;
    }
    views_[index] = nullptr;
    browsers_[index] = nullptr;
    ++closed_browsers_;
    if (closed_browsers_ == 2) {
      result_->browsers_closed = true;
      if (!host_->ReleaseAfterClosed()) {
        passed_ = false;
      }
      if (!window_) {
        CefQuitMessageLoop();
      }
    }
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    bool ready = true;
    for (const auto &browser : browsers_) {
      if (browser && !browser->GetHost()->TryCloseBrowser()) {
        ready = false;
      }
    }
    return ready;
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    result_->behavior_passed = passed_ && state_preserved_;
    std::cout << "alloy_content_view_host_windows passed="
              << result_->behavior_passed
              << " browsers_closed=" << result_->browsers_closed
              << " hidden_window=1" << std::endl;
    window_ = nullptr;
    host_.reset();
    CefQuitMessageLoop();
  }

private:
  ContentViewMountRequest Mount(const char *tab, std::uint64_t epoch) {
    return ContentViewMountRequest{
        Id<ProfileId>("alloy-probe"), Id<TabId>(tab), NavigationId::FromRaw(1),
        Required(MountEpoch::TryCreate(epoch)), ContentPurpose::kWeb};
  }

  int ViewIndex(CefRefPtr<CefBrowserView> view) const {
    for (int index = 0; index < 2; ++index) {
      if (views_[index] && views_[index]->IsSame(view)) {
        return index;
      }
    }
    return -1;
  }

  int BrowserIndex(CefRefPtr<CefBrowser> browser) const {
    for (int index = 0; index < 2; ++index) {
      if (browsers_[index] && browsers_[index]->IsSame(browser)) {
        return index;
      }
    }
    return -1;
  }

  void ScheduleCheck() {
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&AlloyHostProbe::Check, CefRefPtr<AlloyHostProbe>(this)),
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
    if (!window_ || !browsers_[0] || !browsers_[1] || !loaded_[0] ||
        !loaded_[1]) {
      ScheduleCheck();
      return;
    }
    if (stage_ == 0) {
      const auto third = Mount("third", 1);
      CefBrowserSettings settings;
      auto third_view = CefBrowserView::CreateBrowserView(
          this, "about:blank", settings, nullptr, nullptr, this);
      const bool fallback = host_->Mount(third, nullptr) ==
                                ContentViewRegistryResult::kInvalidArgument &&
                            host_->Mount(third, third_view) ==
                                ContentViewRegistryResult::kCapacityExceeded;
      third_view = nullptr;
      wide_content_width_ = host_->container()->GetBounds().width;
      original_urls_[0] = browsers_[0]->GetMainFrame()->GetURL();
      original_urls_[1] = browsers_[1]->GetMainFrame()->GetURL();
      if (!fallback || window_->IsVisible() || wide_content_width_ <= 0 ||
          window_->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY ||
          views_[0]->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY ||
          views_[1]->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY ||
          browsers_[0]->GetHost()->GetRuntimeStyle() !=
              CEF_RUNTIME_STYLE_ALLOY ||
          browsers_[1]->GetHost()->GetRuntimeStyle() !=
              CEF_RUNTIME_STYLE_ALLOY ||
          host_->Activate(first_mount_.tab_id, first_mount_.mount_epoch) !=
              ContentViewRegistryResult::kAccepted ||
          host_->Activate(first_mount_.tab_id,
                          Required(MountEpoch::TryCreate(99))) !=
              ContentViewRegistryResult::kStaleEpoch ||
          host_->SetZoom(first_mount_.tab_id, first_mount_.mount_epoch,
                         Required(ZoomFactor::TryCreate(1.2))) !=
              ContentViewRegistryResult::kAccepted) {
        Finish(false, "fallback-activate-zoom");
        return;
      }
      browsers_[0]->GetMainFrame()->ExecuteJavaScript(
          "window.__alloyProbe=17;document.title='first-state-set';", kFirstUrl,
          1);
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (stage_ == 1) {
      if (!first_state_set_) {
        ScheduleCheck();
        return;
      }
      if (host_->Activate(second_mount_.tab_id, second_mount_.mount_epoch) !=
              ContentViewRegistryResult::kAccepted ||
          host_->SetZoom(second_mount_.tab_id, second_mount_.mount_epoch,
                         Required(ZoomFactor::TryCreate(0.8))) !=
              ContentViewRegistryResult::kAccepted ||
          views_[0]->IsVisible() || !views_[1]->IsVisible()) {
        Finish(false, "second-activation");
        return;
      }
      window_->SetSize(CefSize(kNarrowWidth, kWindowHeight));
      window_->Layout();
      if (host_->container()->GetBounds().width <= 0 ||
          host_->container()->GetBounds().width >= wide_content_width_) {
        Finish(false, "narrow-layout");
        return;
      }
      ++stage_;
    }
    if (stage_ == 2) {
      if (host_->Activate(first_mount_.tab_id, first_mount_.mount_epoch) !=
              ContentViewRegistryResult::kAccepted ||
          !views_[0]->IsVisible() || views_[1]->IsVisible() ||
          browsers_[0]->GetIdentifier() != browser_ids_[0] ||
          browsers_[1]->GetIdentifier() != browser_ids_[1] ||
          browsers_[0]->GetMainFrame()->GetURL() != original_urls_[0] ||
          browsers_[1]->GetMainFrame()->GetURL() != original_urls_[1] ||
          browsers_[0]->GetHost()->GetZoomLevel() <= 0.0 ||
          browsers_[1]->GetHost()->GetZoomLevel() >= 0.0) {
        Finish(false, "identity-preservation");
        return;
      }
      browsers_[0]->GetMainFrame()->ExecuteJavaScript(
          "document.title=window.__alloyProbe===17?"
          "'state-preserved':'state-lost';",
          kFirstUrl, 1);
      ++stage_;
      ScheduleCheck();
      return;
    }
    if (!state_preserved_) {
      ScheduleCheck();
      return;
    }
    Finish(true, "complete");
  }

  void Finish(bool passed, const char *detail) {
    if (finished_) {
      return;
    }
    finished_ = true;
    passed_ = passed;
    std::cout << "alloy_content_view_host_windows detail=" << detail
              << std::endl;
    if (!host_ || !browsers_[0] || !browsers_[1]) {
      CefQuitMessageLoop();
      return;
    }
    for (int index = 0; index < 2; ++index) {
      const auto &mount = index == 0 ? first_mount_ : second_mount_;
      if (host_->BeginClose(mount.tab_id, mount.mount_epoch) !=
          ContentViewRegistryResult::kAccepted) {
        passed_ = false;
      }
    }
    window_->Close();
  }

  std::shared_ptr<AlloyContentViewHostProbeResult> result_;
  std::unique_ptr<AlloyContentViewHost> host_;
  ContentViewMountRequest first_mount_;
  ContentViewMountRequest second_mount_;
  std::array<CefRefPtr<CefBrowserView>, 2> views_;
  std::array<CefRefPtr<CefBrowser>, 2> browsers_;
  std::array<int, 2> browser_ids_{};
  std::array<bool, 2> loaded_{};
  std::array<CefString, 2> original_urls_;
  CefRefPtr<CefWindow> window_;
  int wide_content_width_ = 0;
  int checks_ = 0;
  int stage_ = 0;
  int closed_browsers_ = 0;
  bool first_state_set_ = false;
  bool state_preserved_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(AlloyHostProbe);
};

} // namespace

CefRefPtr<CefApp> CreateAlloyContentViewHostProbe(
    std::shared_ptr<AlloyContentViewHostProbeResult> result) {
  return new AlloyHostProbe(std::move(result));
}
