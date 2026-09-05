#include "alloy_cast_bridge_probe.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "browser/media_host/alloy_cast_controller.h"
#include "browser/media_host/cast_entry_surface.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

namespace cast_view = ::crayon::browser_cast_view;
namespace media_host = ::crayon::browser::cef_shell::media_host;
namespace mh = ::crayon::cef_shell::ipc::media_host;
namespace mh2 = ::crayon::cef_shell::ipc::media_host_v2;
using ::crayon::browser::cef_shell::CastEntrySurface;

constexpr int kPollMs = 20;
constexpr int kMaxPolls = 750;
constexpr int kSpaceKey = 32;

class ProbeTransport final : public media_host::MediaHostTransport {
 public:
  bool Start(std::string) override {
    healthy_ = true;
    ++generation_;
    return true;
  }
  void Stop() override { healthy_ = false; }
  bool Enqueue(mh::Message message) override {
    if (!healthy_) return false;
    sent.push_back(std::move(message));
    return true;
  }
  bool EnqueuePlayer(mh2::PlayerMessage) override { return healthy_; }
  bool EnqueuePlayerList(mh2::PlayerListRequest request) override {
    if (!healthy_) return false;
    player_requests.push_back(std::move(request));
    return true;
  }
  std::vector<mh2::PlayerPageReply> DrainPlayerPages(
      std::size_t maximum) override {
    return Take(&player_replies, maximum);
  }
  bool EnqueueDraft(mh2::DraftCommand command) override {
    if (!healthy_) return false;
    draft_requests.push_back(std::move(command));
    return true;
  }
  std::vector<mh2::DraftStateReply> DrainDraftStates(
      std::size_t maximum) override {
    return Take(&draft_replies, maximum);
  }
  bool supports_player_messages() const noexcept override { return true; }
  bool supports_drafts() const noexcept override { return true; }
  bool supports_connect() const noexcept override { return true; }
  std::uint64_t player_session_id() const noexcept override { return 77; }
  std::vector<mh::Message> Drain(std::size_t maximum) override {
    return Take(&inbound, maximum);
  }
  bool healthy() const noexcept override { return healthy_; }
  std::uint64_t generation() const noexcept override { return generation_; }

  template <typename T>
  static std::vector<T> Take(std::vector<T>* values, std::size_t maximum) {
    const auto count = std::min(maximum, values->size());
    std::vector<T> result;
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(values->front()));
      values->erase(values->begin());
    }
    return result;
  }

  bool healthy_ = false;
  std::uint64_t generation_ = 0;
  std::vector<mh::Message> sent;
  std::vector<mh::Message> inbound;
  std::vector<mh2::PlayerListRequest> player_requests;
  std::vector<mh2::PlayerPageReply> player_replies;
  std::vector<mh2::DraftCommand> draft_requests;
  std::vector<mh2::DraftStateReply> draft_replies;
};

class BridgeProbe final : public CefApp,
                          public CefBrowserProcessHandler,
                          public CefClient,
                          public CefLifeSpanHandler,
                          public CefBrowserViewDelegate,
                          public CefWindowDelegate {
 public:
  explicit BridgeProbe(std::shared_ptr<AlloyCastBridgeProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("use-mock-keychain");
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitchWithValue("force-device-scale-factor", "2");
  }
  void OnContextInitialized() override {
    auto transport = std::make_unique<ProbeTransport>();
    transport_ = transport.get();
    adapter_ = std::make_unique<media_host::MediaHostAdapter>(
        std::move(transport));
    if (!adapter_->Start("fixture-media-host")) {
      result_->window_closed = true;
      CefQuitMessageLoop();
      return;
    }
    surface_ = std::make_unique<CastEntrySurface>(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        [this] { return now_; },
        [this](cast_view::CastSelectionIntent intent) {
          OnIntent(std::move(intent));
        });
    controller_ = std::make_unique<media_host::AlloyCastController>(
        adapter_.get(),
        [this](auto snapshot) {
          snapshot_ = snapshot;
          if (surface_) surface_->Apply(std::move(snapshot));
        },
        "视频", "设备", [this] { return now_; });
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, "about:blank", settings,
                                              nullptr, nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings vertical;
    auto layout = window_->SetToBoxLayout(vertical);
    toolbar_ = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings horizontal;
    horizontal.horizontal = true;
    auto toolbar_layout = toolbar_->SetToBoxLayout(horizontal);
    auto spacer = CefPanel::CreatePanel(nullptr);
    toolbar_->AddChildView(spacer);
    toolbar_layout->SetFlexForView(spacer, 1);
    window_->AddChildView(toolbar_);
    window_->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    if (!surface_->Attach(window_, view_, toolbar_)) {
      Finish(false, "attach");
      return;
    }
    context_ = {1, "fixture", 2, 3, 4};
    surface_->BindContext(context_);
    if (!controller_->BindContext(context_)) {
      Finish(false, "bind");
      return;
    }
    SeedProjection();
    window_->SetSize(CefSize(720, 720));
    window_->Layout();
    window_->Show();
    window_->Activate();
  }
  bool OnKeyEvent(CefRefPtr<CefWindow>, const CefKeyEvent& event) override {
    return surface_ && surface_->HandleKeyEvent(event);
  }
  bool OnAccelerator(CefRefPtr<CefWindow>, int command_id) override {
    return surface_ && surface_->HandleAccelerator(command_id);
  }
  void OnLayoutChanged(CefRefPtr<CefView>, const CefRect&) override {
    if (surface_) surface_->LayoutChanged();
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    if (!browser_ || browser_->GetHost()->TryCloseBrowser()) {
      if (controller_) controller_->Shutdown();
      if (surface_) surface_->Detach();
      return true;
    }
    return false;
  }
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    result_->browser_closed = true;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    controller_.reset();
    adapter_->Stop();
    adapter_.reset();
    surface_.reset();
    toolbar_ = nullptr;
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    CefQuitMessageLoop();
  }

 private:
  void SeedProjection() {
    if (transport_->player_requests.empty()) return;
    const auto request = transport_->player_requests.back();
    transport_->player_replies.push_back(
        {request.context,
         5,
         mh2::PlayerPageStatus::kOk,
         0,
         std::nullopt,
         {{10, 1, mh2::PlayerSourceKind::kHttpUrl, true, true, true, false,
           "https://same.example"},
          {20, 1, mh2::PlayerSourceKind::kHttpUrl, true, true, true, false,
           "https://same.example"},
          {30, 1, mh2::PlayerSourceKind::kHttpUrl, true, true, true, true,
           "https://protected.example"}}});
    std::string request_id;
    for (auto it = transport_->sent.rbegin(); it != transport_->sent.rend();
         ++it) {
      if (const auto* devices = std::get_if<mh::ListDevices>(&*it)) {
        request_id = devices->request_id;
        break;
      }
    }
    if (!request_id.empty()) {
      transport_->inbound.push_back(mh::DevicePageReply{
          request_id, 8, 0, std::nullopt,
          {{"fixture-device", "客厅", mh::DeviceState::kReady, true}}});
    }
    controller_->Tick();
  }

  void OnIntent(cast_view::CastSelectionIntent intent) {
    intents_.push_back(intent.kind);
    const auto draft_count = transport_->draft_requests.size();
    if (!controller_->HandleIntent(intent)) {
      Finish(false, "intent_rejected");
      return;
    }
    if (transport_->draft_requests.size() == draft_count) return;
    const auto& command = transport_->draft_requests.back();
    if (command.action == mh2::DraftAction::kOpen) {
      media_.reset();
      device_.clear();
      connected_ = false;
      draft_revision_ = 1;
    } else {
      ++draft_revision_;
    }
    if (command.action == mh2::DraftAction::kSelectMedia)
      media_ = command.media;
    if (command.action == mh2::DraftAction::kSelectDevice)
      device_ = command.device_id;
    if (command.action == mh2::DraftAction::kConnect) connected_ = true;
    auto phase = mh2::DraftPhase::kChoosing;
    auto route = mh2::DraftRoute::kNone;
    std::optional<std::uint64_t> expiry;
    std::optional<std::uint64_t> session;
    auto error = mh2::DraftError::kNone;
    auto reason = mh2::DraftReason::kNone;
    if (command.action == mh2::DraftAction::kPrepare && fail_prepare_once_) {
      fail_prepare_once_ = false;
      phase = mh2::DraftPhase::kFailed;
      error = mh2::DraftError::kUnavailable;
      reason = mh2::DraftReason::kTimeout;
    } else if (command.action == mh2::DraftAction::kPrepare) {
      phase = mh2::DraftPhase::kPrepared;
      route = mh2::DraftRoute::kDirect;
      expiry = now_ + 10'000;
    } else if (command.action == mh2::DraftAction::kCommit) {
      phase = mh2::DraftPhase::kCommitted;
      session = 47;
    } else if (command.action == mh2::DraftAction::kCancel) {
      phase = mh2::DraftPhase::kCancelled;
    } else if (command.action != mh2::DraftAction::kOpen &&
               command.action != mh2::DraftAction::kSelectMedia &&
               command.action != mh2::DraftAction::kSelectDevice &&
               command.action != mh2::DraftAction::kConnect) {
      return;
    }
    transport_->draft_replies.push_back(
        {command.context, 91, draft_revision_, phase, error, media_, device_,
         connected_, false, route, expiry, reason, session});
    controller_->Tick();
  }

  bool HasIntent(cast_view::CastIntentKind kind) const {
    return std::find(intents_.begin(), intents_.end(), kind) != intents_.end();
  }
  std::size_t CountIntent(cast_view::CastIntentKind kind) const {
    return static_cast<std::size_t>(std::count(intents_.begin(), intents_.end(),
                                               kind));
  }
  CefRefPtr<CefView> Find(int id) {
    auto view = surface_->GetView(id);
    return view ? view : window_->GetViewForID(id);
  }
  bool Press(int id) {
    auto view = Find(id);
    if (!view || !view->IsEnabled() || !view->IsDrawn() || !view->GetWindow())
      return false;
    view->RequestFocus();
    view->GetWindow()->SendKeyPress(kSpaceKey, 0);
    return true;
  }
  bool Await(int id) {
    if (Find(id) && Find(id)->IsEnabled() && Find(id)->IsDrawn()) return false;
    Schedule();
    return true;
  }
  void Schedule() {
    if (finished_ || scheduled_) return;
    scheduled_ = true;
    CefPostDelayedTask(
        TID_UI, base::BindOnce(&BridgeProbe::RunCheck, CefRefPtr(this)),
        kPollMs);
  }
  void RunCheck() {
    scheduled_ = false;
    Check();
  }
  void Finish(bool passed, const char* reason) {
    if (finished_) return;
    finished_ = true;
    std::cout << "alloy_cast_bridge_probe stage=" << stage_
              << " result=" << reason << std::endl;
    if (!passed) {
      result_->selection_passed = result_->connection_passed =
          result_->reason_passed = result_->session_passed =
              result_->accessibility_passed = false;
    }
    window_->Close();
  }
  void Check() {
    if (finished_) return;
    if (++polls_ > kMaxPolls) {
      std::cout << "alloy_cast_bridge_probe timeout phase="
                << static_cast<int>(snapshot_.phase)
                << " media=" << snapshot_.selected_media.has_value()
                << " device=" << snapshot_.selected_device.has_value()
                << " connected=" << snapshot_.device_connected
                << " draft=" << snapshot_.draft_id << '/'
                << snapshot_.draft_revision << std::endl;
      Finish(false, "timeout");
      return;
    }
    if (!browser_) {
      Schedule();
      return;
    }
    controller_->Tick();
    surface_->Tick();
    switch (stage_) {
      case 0:
        if (Await(CastEntrySurface::kEntryId)) return;
        if (snapshot_.media_total != 3 || snapshot_.eligible_count != 2 ||
            !Press(CastEntrySurface::kEntryId)) {
          Finish(false, "entry");
          return;
        }
        break;
      case 1:
        if (Await(CastEntrySurface::kMediaFirstId + 1)) return;
        if (!Press(CastEntrySurface::kMediaFirstId + 1)) {
          Finish(false, "media");
          return;
        }
        break;
      case 2:
        if (!HasIntent(cast_view::CastIntentKind::kSelectMedia)) {
          Schedule();
          return;
        }
        if (Await(CastEntrySurface::kDeviceFirstId)) return;
        if (!Press(CastEntrySurface::kDeviceFirstId)) {
          Finish(false, "device");
          return;
        }
        break;
      case 3:
        if (!HasIntent(cast_view::CastIntentKind::kSelectDevice)) {
          Schedule();
          return;
        }
        if (Await(CastEntrySurface::kConnectId)) return;
        if (!Press(CastEntrySurface::kConnectId)) {
          Finish(false, "connect");
          return;
        }
        break;
      case 4:
        if (!HasIntent(cast_view::CastIntentKind::kConnectDevice)) {
          Schedule();
          return;
        }
        if (Await(CastEntrySurface::kPrepareId)) return;
        {
          const bool started = std::any_of(
              transport_->sent.begin(), transport_->sent.end(),
              [](const auto& message) {
                return std::holds_alternative<mh::StartCast>(message);
              });
          const bool pressed = Press(CastEntrySurface::kPrepareId);
          if (!connected_ || started || !pressed) {
            std::cout << "bridge connect evidence connected=" << connected_
                      << " started=" << started << " pressed=" << pressed
                      << std::endl;
            Finish(false, "connect_started_playback");
            return;
          }
        }
        result_->connection_passed = true;
        break;
      case 5:
        if (CountIntent(cast_view::CastIntentKind::kPrepare) != 1) {
          Schedule();
          return;
        }
        {
          auto status = Find(CastEntrySurface::kStatusId);
          auto field = status ? status->AsTextfield() : nullptr;
          if (!field || field->GetText().ToString() != "媒体检查超时") {
            Schedule();
            return;
          }
          if (Await(CastEntrySurface::kPrepareId)) return;
          if (!Press(CastEntrySurface::kPrepareId)) {
            Finish(false, "retry_prepare");
            return;
          }
          result_->reason_passed = true;
        }
        break;
      case 6:
        if (CountIntent(cast_view::CastIntentKind::kPrepare) != 2) {
          Schedule();
          return;
        }
        if (Await(CastEntrySurface::kCommitId)) return;
        if (!Press(CastEntrySurface::kCommitId)) {
          Finish(false, "commit");
          return;
        }
        result_->selection_passed =
            snapshot_.selected_media &&
            snapshot_.selected_media->ref.instance_id == 20 &&
            snapshot_.selected_device &&
            snapshot_.selected_device->id == "fixture-device";
        break;
      case 7:
        if (Await(CastEntrySurface::kSessionControlId)) return;
        if (!Press(CastEntrySurface::kSessionControlId)) {
          Finish(false, "pause");
          return;
        }
        break;
      case 8:
        if (!HasIntent(cast_view::CastIntentKind::kPause)) {
          Schedule();
          return;
        }
        if (Await(CastEntrySurface::kStopId)) return;
        if (!std::any_of(transport_->sent.begin(), transport_->sent.end(),
                         [](const auto& message) {
              const auto* control = std::get_if<mh::ControlCast>(&message);
              return control && control->session_generation == 47 &&
                     control->action == mh::CastControlAction::kPause;
            }) ||
            !Press(CastEntrySurface::kStopId)) {
          Finish(false, "session_control");
          return;
        }
        break;
      case 9: {
        if (!HasIntent(cast_view::CastIntentKind::kStop)) {
          Schedule();
          return;
        }
        if (transport_->sent.empty() ||
            !std::holds_alternative<mh::StopCast>(transport_->sent.back()) ||
            std::get<mh::StopCast>(transport_->sent.back())
                    .session_generation != 47 ||
            !HasIntent(cast_view::CastIntentKind::kCommit) ||
            !HasIntent(cast_view::CastIntentKind::kPause) ||
            !HasIntent(cast_view::CastIntentKind::kStop)) {
          Finish(false, "session_generation");
          return;
        }
        auto entry = Find(CastEntrySurface::kEntryId);
        result_->session_passed = true;
        result_->accessibility_passed =
            entry && window_->GetBounds().width == 720 &&
            HasIntent(cast_view::CastIntentKind::kSelectMedia) &&
            HasIntent(cast_view::CastIntentKind::kSelectDevice);
        Finish(result_->selection_passed && result_->connection_passed &&
                   result_->reason_passed && result_->session_passed &&
                   result_->accessibility_passed,
               "PASS");
        return;
      }
    }
    ++stage_;
    Schedule();
  }

  std::shared_ptr<AlloyCastBridgeProbeResult> result_;
  ProbeTransport* transport_ = nullptr;
  std::unique_ptr<media_host::MediaHostAdapter> adapter_;
  std::unique_ptr<media_host::AlloyCastController> controller_;
  std::unique_ptr<CastEntrySurface> surface_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  cast_view::CastViewContext context_;
  cast_view::CastSelectionSnapshot snapshot_;
  std::vector<cast_view::CastIntentKind> intents_;
  std::optional<mh2::DraftMediaRef> media_;
  std::string device_;
  std::uint64_t draft_revision_ = 0;
  std::uint64_t now_ = 1000;
  int stage_ = 0;
  int polls_ = 0;
  bool connected_ = false;
  bool fail_prepare_once_ = true;
  bool scheduled_ = false;
  bool finished_ = false;
  IMPLEMENT_REFCOUNTING(BridgeProbe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyCastBridgeProbe(
    std::shared_ptr<AlloyCastBridgeProbeResult> result) {
  return new BridgeProbe(std::move(result));
}
