#include "alloy_cast_toolbar_mac_probe.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "browser/media_host/alloy_cast_controller.h"
#include "browser/media_host/cast_entry_surface.h"
#include "browser/window/tab_controller.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_button_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "macos/alloy_product_host_mac.h"
#include "macos/alloy_toolbar_mac.h"

namespace {
namespace shell = crayon::browser::cef_shell;
namespace mh = crayon::cef_shell::ipc::media_host;
namespace mh2 = crayon::cef_shell::ipc::media_host_v2;
namespace cv = crayon::browser_cast_view;
using shell::CastEntrySurface;

// Only the remote transport is fake: window, toolbar, TabController, surface,
// adapter and controller are the production implementations.
class Transport final : public shell::media_host::MediaHostTransport {
 public:
  bool Start(std::string) override { return true; }
  void Stop() override {}
  bool healthy() const noexcept override { return true; }
  std::uint64_t generation() const noexcept override { return 1; }
  bool supports_player_messages() const noexcept override { return true; }
  bool supports_drafts() const noexcept override { return true; }
  bool supports_connect() const noexcept override { return true; }
  std::uint64_t player_session_id() const noexcept override { return 77; }
  bool Enqueue(mh::Message message) override {
    sent.push_back(std::move(message));
    return true;
  }
  std::vector<mh::Message> Drain(std::size_t) override { return {}; }
  bool EnqueuePlayer(mh2::PlayerMessage) override { return true; }
  bool EnqueuePlayerList(mh2::PlayerListRequest request) override {
    pages.push_back({request.context,
                     ++revision,
                     mh2::PlayerPageStatus::kOk,
                     0,
                     std::nullopt,
                     {}});
    if (media_available)
      pages.back().players.push_back({8, 1, mh2::PlayerSourceKind::kHttpUrl,
                                      true, true, true, false, "Video"});
    return true;
  }
  std::vector<mh2::PlayerPageReply> DrainPlayerPages(std::size_t) override {
    return std::exchange(pages, {});
  }
  bool EnqueueDraft(mh2::DraftCommand command) override {
    drafts.push_back(std::move(command));
    return true;
  }
  std::vector<mh2::DraftStateReply> DrainDraftStates(std::size_t) override {
    return {};
  }
  bool media_available = false;
  std::uint64_t revision = 0;
  std::vector<mh::Message> sent;
  std::vector<mh2::PlayerPageReply> pages;
  std::vector<mh2::DraftCommand> drafts;
};

class Probe final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit Probe(std::shared_ptr<AlloyCastToolbarMacProbeResult> result)
      : result_(std::move(result)) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("use-mock-keychain");
    command->AppendSwitch("disable-background-networking");
  }
  void OnContextInitialized() override {
    const auto locale = crayon::browser::localization::SnapshotFor(
        crayon::browser::localization::AppLocale::kZhCn);
    auto transport = std::make_unique<Transport>();
    transport_ = transport.get();
    adapter_ = std::make_unique<shell::media_host::MediaHostAdapter>(
        std::move(transport));
    adapter_->Start("fixture");
    controller_ = std::make_unique<shell::media_host::AlloyCastController>(
        adapter_.get(),
        [this](auto snapshot) {
          if (surface_) surface_->Apply(std::move(snapshot));
        },
        "视频", "设备", [this] { return now_; });
    tabs_ = new shell::window::TabController("about:blank");
    tabs_->SetMediaObservationLifecycleCallback(
        [this](std::uint32_t tab, std::uint64_t, std::uint32_t generation,
               bool closed) {
          if (closed)
            generations_.erase(tab);
          else
            generations_[tab] = generation;
        });
    tabs_->SetBrowserClosingCallback(
        [this](CefRefPtr<CefBrowser>) { Detach(); });
    toolbar_ = std::make_unique<shell::macos::AlloyToolbarMac>(
        locale, shell::macos::AlloyToolbarMac::Callbacks{});
    shell::macos::AlloyProductHostMac::Callbacks callbacks;
    callbacks.before_close = [this] { Detach(); };
    callbacks.window_destroyed = [this] {
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &Probe::Cleanup, CefRefPtr<Probe>(this))));
    };
    callbacks.layout_changed = [this] {
      if (surface_) surface_->LayoutChanged();
    };
    callbacks.key_event = [this](const CefKeyEvent& e) {
      return surface_ && surface_->HandleKeyEvent(e);
    };
    callbacks.accelerator = [this](int id) {
      return surface_ && surface_->HandleAccelerator(id);
    };
    host_ = std::make_unique<shell::macos::AlloyProductHostMac>(
        shell::macos::AlloyProductHostMac::Dependencies{
            tabs_->client(), "about:blank", "Cast toolbar probe",
            toolbar_->tab_strip_view(), toolbar_->toolbar_view()},
        std::move(callbacks));
    if (!host_->Start()) {
      Fail("start");
      return;
    }
    Schedule();
  }

 private:
  void Cleanup() {
    Detach();
    toolbar_->Shutdown();
    host_.reset();
    toolbar_.reset();
    tabs_ = nullptr;
    result_->closed = true;
  }
  void Detach() {
    if (surface_) surface_->Detach();
    surface_.reset();
  }
  bool Bind() {
    const auto browser = tabs_->ActiveBrowser();
    if (!browser || browser->IsLoading()) return false;
    const auto* tab = tabs_->model().FindByBrowser(browser->GetIdentifier());
    if (!tab || !generations_.count(tab->id)) return false;
    const auto view = host_->browser_view(browser->GetIdentifier());
    if (!view || !host_->window()) return false;
    if (active_browser_ != browser->GetIdentifier()) {
      active_browser_ = browser->GetIdentifier();
      tabs_->client()->AdvanceMediaObservationNavigation(
          browser, static_cast<std::uint32_t>(tab->id),
          tab->navigation_generation);
    }
    const cv::CastViewContext next{
        9, "fixture", static_cast<std::uint32_t>(tab->id),
        tab->navigation_generation, generations_.at(tab->id)};
    if (context_ && *context_ == next) return true;
    Detach();
    toolbar_->AttachBrowser(tab->id, browser);
    toolbar_->SyncTabs(tabs_->model());
    host_->ShowBrowser(browser->GetIdentifier());
    surface_ = std::make_unique<CastEntrySurface>(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        [this] { return now_; },
        [this](auto intent) {
          if (!controller_->HandleIntent(intent)) Fail("intent rejected");
        });
    if (!surface_->Attach(host_->window(), view, toolbar_->toolbar_panel())) {
      Fail("attach");
      return false;
    }
    surface_->BindContext(next);
    if (!controller_->BindContext(next)) {
      Fail("bind");
      return false;
    }
    context_ = next;
    return true;
  }
  std::size_t EntryCount() {
    std::size_t count = 0;
    auto panel = toolbar_->toolbar_panel();
    for (std::size_t i = 0; i < panel->GetChildViewCount(); ++i)
      if (panel->GetChildViewAt(i)->GetID() == CastEntrySurface::kEntryId)
        ++count;
    return count;
  }
  void Schedule() {
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(
                           &Probe::Tick, CefRefPtr<Probe>(this))),
                       20);
  }
  void Fail(const char* detail) {
    std::cerr << "alloy_cast_toolbar_mac FAIL " << detail << '\n';
    finished_ = true;
    Detach();
    if (controller_) controller_->Shutdown();
    if (host_) host_->Close();
  }
  void Tick() {
    if (finished_) return;
    if (++polls_ > 500) {
      Fail("timeout");
      return;
    }
    if (!Bind()) {
      Schedule();
      return;
    }
    controller_->Tick();
    surface_->Tick();
    if (EntryCount() != 1) {
      Fail("entry count");
      return;
    }
    auto entry = surface_->GetView(CastEntrySurface::kEntryId);
    switch (phase_) {
      case 0:
        if (!entry || entry->IsEnabled()) {
          Fail("initial disabled");
          return;
        }
        first_ = *context_;
        transport_->media_available = true;
        now_ += 1000;
        ++phase_;
        break;
      case 1:
        if (!entry->IsEnabled()) break;
        // Invoke the real native button delegate without OS input permissions.
        static_cast<CefButtonDelegate*>(entry->GetDelegate().get())
            ->OnButtonPressed(entry->AsButton());
        ++phase_;
        break;
      case 2:
        if (transport_->drafts.empty()) break;
        if (transport_->drafts.size() != 1 ||
            transport_->drafts.back().action != mh2::DraftAction::kOpen ||
            transport_->drafts.back().context.tab_id != first_.tab_id) {
          Fail("open intent");
          return;
        }
        stale_ = controller_->snapshot();
        transport_->media_available = false;
        tabs_->ActiveBrowser()->GetMainFrame()->LoadURL(
            "data:text/html,second");
        ++phase_;
        break;
      case 3:
        if (context_->navigation_id == first_.navigation_id) break;
        if (entry->IsEnabled()) {
          Fail("navigation not fenced");
          return;
        }
        if (surface_->Apply(stale_)) {
          Fail("stale snapshot accepted");
          return;
        }
        if (!host_->CreateTab("about:blank")) {
          Fail("create tab");
          return;
        }
        ++phase_;
        break;
      case 4:
        if (context_->tab_id == first_.tab_id) break;
        if (entry->IsEnabled()) {
          Fail("new tab not disabled");
          return;
        }
        if (!tabs_->ActivateTab(first_.tab_id)) {
          Fail("activate");
          return;
        }
        ++phase_;
        break;
      case 5:
        if (context_->tab_id != first_.tab_id) break;
        if (std::none_of(transport_->sent.begin(), transport_->sent.end(),
                         [](const auto& message) {
                           return std::holds_alternative<mh::CloseTab>(message);
                         })) {
          Fail("missing CloseTab");
          return;
        }
        finished_ = true;
        Detach();
        controller_->Shutdown();
        result_->passed = EntryCount() == 0;
        std::cout << "alloy_cast_toolbar_mac "
                  << (result_->passed ? "PASS" : "FAIL") << '\n';
        std::cout.flush();
        host_->Close();
        return;
    }
    Schedule();
  }
  std::shared_ptr<AlloyCastToolbarMacProbeResult> result_;
  Transport* transport_ = nullptr;
  std::unique_ptr<shell::media_host::MediaHostAdapter> adapter_;
  std::unique_ptr<shell::media_host::AlloyCastController> controller_;
  std::unique_ptr<shell::macos::AlloyToolbarMac> toolbar_;
  std::unique_ptr<shell::macos::AlloyProductHostMac> host_;
  std::unique_ptr<CastEntrySurface> surface_;
  CefRefPtr<shell::window::TabController> tabs_;
  std::map<std::uint32_t, std::uint32_t> generations_;
  std::optional<cv::CastViewContext> context_;
  cv::CastViewContext first_;
  cv::CastSelectionSnapshot stale_;
  std::uint64_t now_ = 1000;
  int polls_ = 0;
  int phase_ = 0;
  int active_browser_ = 0;
  bool finished_ = false;
  IMPLEMENT_REFCOUNTING(Probe);
};
}  // namespace

CefRefPtr<CefApp> CreateAlloyCastToolbarMacProbe(
    std::shared_ptr<AlloyCastToolbarMacProbeResult> result) {
  return new Probe(std::move(result));
}
