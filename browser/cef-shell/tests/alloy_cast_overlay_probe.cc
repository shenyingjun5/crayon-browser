#include "alloy_cast_overlay_probe.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "browser/media_host/alloy_cast_controller.h"
#include "browser/observation_gateway/cef_observation_bridge.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "windows/alloy_cast_overlay_win.h"

namespace {

namespace cast_view = ::crayon::browser_cast_view;
namespace media_host = ::crayon::browser::cef_shell::media_host;
namespace mh = ::crayon::cef_shell::ipc::media_host;
namespace mh2 = ::crayon::cef_shell::ipc::media_host_v2;
using ::crayon::browser::cef_shell::observation::CefObservationBridge;
using ::crayon::browser::cef_shell::windows::AlloyCastOverlayObservation;
using ::crayon::browser::cef_shell::windows::AlloyCastOverlayWin;
using ::crayon::cef_shell::gateway::EventSource;
using ::crayon::cef_shell::gateway::GatewayEvent;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumPolls = 750;

std::uint64_t NowMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

class OverlayTransport final : public media_host::MediaHostTransport {
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

class OverlayProbe final : public CefApp,
                           public CefBrowserProcessHandler,
                           public CefClient,
                           public CefLifeSpanHandler,
                           public CefLoadHandler,
                           public CefDisplayHandler,
                           public CefBrowserViewDelegate,
                           public CefWindowDelegate {
 public:
  OverlayProbe(std::string fixture_url,
               std::shared_ptr<AlloyCastOverlayProbeResult> result)
      : fixture_url_(std::move(fixture_url)), result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-default-apps");
    command->AppendSwitch("disable-sync");
    command->AppendSwitch("no-proxy-server");
    command->AppendSwitchWithValue("force-device-scale-factor", "2");
  }

  void OnContextInitialized() override {
    auto transport = std::make_unique<OverlayTransport>();
    transport_ = transport.get();
    adapter_ = std::make_unique<media_host::MediaHostAdapter>(
        std::move(transport));
    if (!adapter_->Start("fixture-media-host")) {
      Finish(false, "adapter-start");
      return;
    }
    controller_ = std::make_unique<media_host::AlloyCastController>(
        adapter_.get(),
        [this](auto snapshot) {
          snapshot_ = snapshot;
          if (overlay_) overlay_->Apply(std::move(snapshot));
        },
        "视频", "设备", [this] { return now_; });
    overlay_ = std::make_unique<AlloyCastOverlayWin>(
        L"投屏此视频", [this] { return now_; },
        [this](cast_view::CastMediaRef media) {
          clicked_media_ = media;
          return controller_ && controller_->OpenForMedia(media);
        });
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, fixture_url_, settings,
                                              nullptr, nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(view_);
    layout->SetFlexForView(view_, 1);
    window_->SetSize(CefSize(960, 720));
    window_->Layout();
    window_->Show();
    window_->Activate();
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    observation_.AdvanceNavigation(browser_, context_.tab_id,
                                   context_.navigation_id);
    observation_.SetActiveTab(context_.tab_id);
    observation_.BindCurrentMainFrame(browser_);
    if (!controller_->BindContext(context_)) Finish(false, "context");
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame || !frame->IsMain() || http_status_code != 200) return;
    observation_.BindCurrentMainFrame(browser);
    observation_.NoteTrustedUserInput(browser);
    frame->ExecuteJavaScript(
        "document.getElementById('start-playback').click();", frame->GetURL(),
        1);
  }

  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                   ErrorCode error_code, const CefString&,
                   const CefString& failed_url) override {
    if (frame && frame->IsMain() && error_code != ERR_ABORTED) {
      std::cerr << "alloy_overlay_load_error=" << error_code
                << " url=" << failed_url.ToString() << std::endl;
      Finish(false, "load-error");
    }
  }

  bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t level,
                        const CefString& message, const CefString& source,
                        int line) override {
    if (level >= LOGSEVERITY_ERROR) {
      std::cerr << "alloy_overlay_console_error=" << message.ToString()
                << " source=" << source.ToString() << " line=" << line
                << std::endl;
      Finish(false, "console-error");
    }
    return true;
  }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    return observation_.OnProcessMessageReceived(
        std::move(browser), std::move(frame), source_process,
        std::move(message));
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    if (!browser_ || browser_->GetHost()->TryCloseBrowser()) {
      if (overlay_) overlay_->Detach();
      if (controller_) controller_->Shutdown();
      observation_.CloseBrowser(browser_, context_.tab_id);
      return true;
    }
    return false;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser>) override {
    browser_ = nullptr;
    result_->browser_closed = true;
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    overlay_.reset();
    controller_.reset();
    adapter_->Stop();
    adapter_.reset();
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    CefQuitMessageLoop();
  }

 private:
  void Schedule() {
    if (finished_ || scheduled_) return;
    scheduled_ = true;
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&OverlayProbe::Check, CefRefPtr(this)),
                       kPollMilliseconds);
  }

  void Finish(bool passed, const char* reason) {
    if (finished_) return;
    finished_ = true;
    std::cout << "alloy_cast_overlay_probe stage=" << stage_
              << " result=" << reason << std::endl;
    if (!passed) {
      result_->geometry_passed = result_->native_surface_passed =
          result_->keyboard_intent_passed = result_->stale_and_focus_passed =
              result_->occlusion_and_navigation_passed = false;
    }
    if (window_) window_->Close();
    else CefQuitMessageLoop();
  }

  void ConsumeGeometry() {
    for (GatewayEvent& event : observation_.Drain(64)) {
      if (event.source != EventSource::kMedia || event.player_removed ||
          !event.player_reference) {
        continue;
      }
      latest_event_ = std::move(event);
    }
  }

  bool AttachOverlay() {
    if (attached_ || !window_ || !browser_) return attached_;
    HWND root = window_->GetWindowHandle();
    HWND browser = browser_->GetHost()->GetWindowHandle();
    attached_ = overlay_->Attach(root, browser);
    if (attached_) overlay_->BindContext(context_);
    return attached_;
  }

  void SeedProjection(const GatewayEvent& event) {
    if (projection_seeded_ || transport_->player_requests.empty() ||
        !event.player_reference) {
      return;
    }
    const auto request = transport_->player_requests.back();
    const auto& reference = *event.player_reference;
    transport_->player_replies.push_back(
        {request.context,
         5,
         mh2::PlayerPageStatus::kOk,
         0,
         std::nullopt,
         {{reference.instance_id, reference.source_revision,
           mh2::PlayerSourceKind::kHttpUrl, true, true, true, false,
           "Video"}}});
    controller_->Tick();
    projection_seeded_ = snapshot_.compatible && snapshot_.eligible_count == 1;
  }

  AlloyCastOverlayObservation OverlayObservation(const GatewayEvent& event,
                                                  std::uint64_t expires) const {
    AlloyCastOverlayObservation value;
    value.anchor.context = context_;
    value.anchor.view_revision = snapshot_.view_revision;
    value.anchor.media = {event.player_reference->instance_id,
                          event.player_reference->source_revision};
    value.anchor.expires_at_ms = expires;
    value.anchor.supported = event.media.geometry_supported;
    value.anchor.x = event.media.geometry_x;
    value.anchor.y = event.media.geometry_y;
    value.anchor.width = event.media.geometry_width;
    value.anchor.height = event.media.geometry_height;
    value.viewport_width = event.media.viewport_width;
    value.viewport_height = event.media.viewport_height;
    return value;
  }

  void Check() {
    scheduled_ = false;
    if (finished_) return;
    if (++polls_ > kMaximumPolls) {
      const auto diagnostics = observation_.diagnostics();
      std::cout << "alloy_cast_overlay_diagnostics received="
                << diagnostics.received_total
                << " accepted=" << diagnostics.accepted_current_total
                << " eligible=" << diagnostics.eligible_total
                << " proof_denied=" << diagnostics.proof_denied_total
                << " latest=" << latest_event_.has_value();
      if (latest_event_) {
        std::cout << " supported=" << latest_event_->media.geometry_supported
                  << " rect=" << latest_event_->media.geometry_x << ','
                  << latest_event_->media.geometry_y << ','
                  << latest_event_->media.geometry_width << ','
                  << latest_event_->media.geometry_height << " viewport="
                  << latest_event_->media.viewport_width << 'x'
                  << latest_event_->media.viewport_height;
      }
      if (window_ && browser_) {
        HWND root = window_->GetWindowHandle();
        HWND child = browser_->GetHost()->GetWindowHandle();
        std::cout << " root=" << root << " browser=" << child
                  << " parent=" << GetParent(child)
                  << " ancestor=" << GetAncestor(child, GA_ROOT)
                  << " is_child=" << IsChild(root, child)
                  << " attached=" << attached_;
      }
      std::cout << std::endl;
      Finish(false, "timeout");
      return;
    }
    now_ = NowMilliseconds();
    ConsumeGeometry();
    if (!AttachOverlay()) {
      Schedule();
      return;
    }
    controller_->Tick();
    overlay_->Tick();
    switch (stage_) {
      case 0:
        if (!latest_event_ || !latest_event_->media.geometry_supported ||
            !latest_event_->player_reference) {
          Schedule();
          return;
        }
        result_->geometry_passed = latest_event_->media.geometry_width >= 600 &&
                                   latest_event_->media.geometry_height >= 300;
        SeedProjection(*latest_event_);
        if (!projection_seeded_) {
          Schedule();
          return;
        }
        overlay_->SetObservations(
            {OverlayObservation(*latest_event_, now_ + 500)});
        break;
      case 1: {
        HWND root = window_->GetWindowHandle();
        HWND button = GetDlgItem(root, AlloyCastOverlayWin::kFirstControlId);
        wchar_t text[64]{};
        if (!button || !IsWindowVisible(button) ||
            GetWindowTextW(button, text, static_cast<int>(std::size(text))) <=
                0 ||
            std::wstring(text) != L"投屏此视频") {
          Schedule();
          return;
        }
        result_->native_surface_passed = overlay_->visible_count() == 1;
        SendMessageW(root, WM_ACTIVATE, WA_INACTIVE, 0);
        if (overlay_->visible_count() != 0) {
          Finish(false, "focus-hide");
          return;
        }
        SendMessageW(root, WM_ACTIVATE, WA_ACTIVE, 0);
        if (overlay_->visible_count() != 1) {
          Finish(false, "focus-restore");
          return;
        }
        now_ += 501;
        overlay_->Tick();
        if (overlay_->visible_count() != 0) {
          Finish(false, "stale-visible");
          return;
        }
        result_->stale_and_focus_passed = true;
        latest_event_.reset();
        browser_->GetMainFrame()->ExecuteJavaScript(
            "(()=>{const v=document.querySelector('video');const r=v."
            "getBoundingClientRect();const b=document.createElement('div');"
            "b.id='overlay-blocker';b.style.cssText=`position:fixed;z-index:"
            "9999;left:${r.left}px;top:${r.top}px;width:${r.width}px;height:"
            "${r.height}px;background:black`;document.body.append(b)})()",
            browser_->GetMainFrame()->GetURL(), 1);
        break;
      }
      case 2:
        if (!latest_event_ || latest_event_->media.geometry_supported) {
          Schedule();
          return;
        }
        overlay_->SetObservations(
            {OverlayObservation(*latest_event_, now_ + 500)});
        if (overlay_->visible_count() != 0) {
          Finish(false, "occluded-visible");
          return;
        }
        latest_event_.reset();
        browser_->GetMainFrame()->ExecuteJavaScript(
            "document.getElementById('overlay-blocker')?.remove()",
            browser_->GetMainFrame()->GetURL(), 1);
        break;
      case 3: {
        if (!latest_event_ || !latest_event_->media.geometry_supported) {
          Schedule();
          return;
        }
        overlay_->SetObservations(
            {OverlayObservation(*latest_event_, now_ + 500)});
        HWND root = window_->GetWindowHandle();
        HWND button = GetDlgItem(root, AlloyCastOverlayWin::kFirstControlId);
        if (!button || !IsWindowVisible(button)) {
          Schedule();
          return;
        }
        SetFocus(button);
        SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
        break;
      }
      case 4:
        if (transport_->draft_requests.empty()) {
          Schedule();
          return;
        }
        if (!clicked_media_ ||
            transport_->draft_requests.back().action !=
                mh2::DraftAction::kOpen) {
          Finish(false, "open-intent");
          return;
        }
        transport_->draft_replies.push_back(
            {transport_->draft_requests.back().context,
             91,
             1,
             mh2::DraftPhase::kChoosing,
             mh2::DraftError::kNone,
             std::nullopt,
             {},
             false,
             false,
             mh2::DraftRoute::kNone,
             std::nullopt,
             mh2::DraftReason::kNone,
             std::nullopt});
        controller_->Tick();
        if (transport_->draft_requests.back().action !=
                mh2::DraftAction::kSelectMedia ||
            !transport_->draft_requests.back().media ||
            transport_->draft_requests.back().media->instance_id !=
                clicked_media_->instance_id ||
            transport_->draft_requests.back().media->source_revision !=
                clicked_media_->source_revision) {
          Finish(false, "media-revalidation");
          return;
        }
        result_->keyboard_intent_passed = true;
        context_.navigation_id++;
        context_.generation++;
        if (!controller_->BindContext(context_)) {
          Finish(false, "navigation-context");
          return;
        }
        overlay_->BindContext(context_);
        if (overlay_->visible_count() != 0 ||
            controller_->OpenForMedia(*clicked_media_)) {
          Finish(false, "navigation-fence");
          return;
        }
        result_->occlusion_and_navigation_passed = true;
        Finish(result_->geometry_passed && result_->native_surface_passed &&
                   result_->keyboard_intent_passed &&
                   result_->stale_and_focus_passed &&
                   result_->occlusion_and_navigation_passed,
               "PASS");
        return;
    }
    ++stage_;
    Schedule();
  }

  std::string fixture_url_;
  std::shared_ptr<AlloyCastOverlayProbeResult> result_;
  CefObservationBridge observation_;
  OverlayTransport* transport_ = nullptr;
  std::unique_ptr<media_host::MediaHostAdapter> adapter_;
  std::unique_ptr<media_host::AlloyCastController> controller_;
  std::unique_ptr<AlloyCastOverlayWin> overlay_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  cast_view::CastViewContext context_{1, "fixture", 2, 3, 1};
  cast_view::CastSelectionSnapshot snapshot_;
  std::optional<GatewayEvent> latest_event_;
  std::optional<cast_view::CastMediaRef> clicked_media_;
  std::uint64_t now_ = 0;
  int stage_ = 0;
  int polls_ = 0;
  bool attached_ = false;
  bool projection_seeded_ = false;
  bool scheduled_ = false;
  bool finished_ = false;
  IMPLEMENT_REFCOUNTING(OverlayProbe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyCastOverlayProbe(
    std::string fixture_url,
    std::shared_ptr<AlloyCastOverlayProbeResult> result) {
  return new OverlayProbe(std::move(fixture_url), std::move(result));
}
