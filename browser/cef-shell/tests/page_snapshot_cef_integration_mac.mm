#import <Cocoa/Cocoa.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "browser/window/tab_controller.h"
#include "browser/media_host/media_host_adapter.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_application_mac.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"
#include "ipc/media_observation_cef_message.h"
#include "macos/cast_chrome_mac.h"
#include "macos/cast_shell_controller.h"
#include "macos/content_host_adapter_mac.h"
#include "macos/trusted_input_monitor_mac.h"

#ifndef CRAYON_SNAPSHOT_TEST_HELPER_PATH
#error "CRAYON_SNAPSHOT_TEST_HELPER_PATH must be defined"
#endif

#ifndef CRAYON_CONTENT_HOST_TEST_PATH
#error "CRAYON_CONTENT_HOST_TEST_PATH must be defined"
#endif

#ifndef CRAYON_MEDIA_HOST_TEST_PATH
#error "CRAYON_MEDIA_HOST_TEST_PATH must be defined"
#endif

namespace {

namespace host = crayon::browser::cef_shell::macos::content_host_ipc;
using crayon::browser::cef_shell::media_host::BrowserMediaFact;
using crayon::browser::cef_shell::macos::CastChromeCallbacks;
using crayon::browser::cef_shell::macos::CastChromeMac;
using crayon::browser::cef_shell::macos::CastChromeStrings;
using crayon::browser::cef_shell::macos::CastCommandPort;
using crayon::browser::cef_shell::macos::CastShellController;
using crayon::browser::cef_shell::macos::ContentHostAdapter;
using crayon::browser::cef_shell::macos::ContentHostProcess;
using crayon::browser::cef_shell::macos::ContentHostTransport;
using crayon::browser::cef_shell::media_host::MediaHostAdapter;
using crayon::browser::cef_shell::macos::MediaHostProcess;
using crayon::browser::cef_shell::media_host::MediaPlanningEventKind;
using crayon::browser::cef_shell::macos::TrustedInputMonitor;
using crayon::browser::cef_shell::window::TabController;
using crayon::browser_engine::EngineErrorCode;
using crayon::browser_engine::SnapshotRequestId;
using crayon::browser_engine::SnapshotTerminal;
using crayon::browser_engine::SnapshotTerminalStatus;
using crayon::cef_shell::gateway::EventSource;
using crayon::cef_shell::gateway::GatewayEvent;
using crayon::cef_shell::gateway::SnapshotGatewayEvent;

constexpr std::int64_t kTickMilliseconds = 20;
constexpr std::size_t kStartupChecks = 500;
constexpr std::size_t kNoReplyChecks = 25;

std::uint64_t MonotonicMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::string SiblingUrl(const std::string &url, const char *filename) {
  const std::size_t slash = url.rfind('/');
  return slash == std::string::npos ? std::string{} : url.substr(0, slash + 1) + filename;
}

bool IsAutomatedMediaScenario(const std::string &scenario) {
  return scenario.rfind("media-", 0) == 0 && scenario != "media-manual" &&
         scenario != "media-forged";
}

bool ValidateMediaIpcContracts() {
  namespace media_ipc = crayon::browser::cef_shell::media_ipc;
  media_ipc::MediaObservationEnvelope envelope;
  envelope.observation.navigation_id = 42;
  envelope.observation.element_id = 7;
  envelope.observation.playback =
      crayon::cef_shell::renderer::MediaPlaybackState::kPlaying;
  envelope.observation.source_kind =
      crayon::cef_shell::renderer::MediaSourceKind::kHttpUrl;
  envelope.observation.source_url = "https://fixture.invalid/clear.mp4";
  envelope.observation.visible_fraction = 0.75;
  envelope.observation.current_time_seconds = 1.25;
  const auto advance = media_ipc::ReadAdvanceMessage(
      media_ipc::CreateAdvanceMessage(envelope.observation.navigation_id));
  const auto decoded = media_ipc::ReadObservationMessage(
      media_ipc::CreateObservationMessage(envelope));
  if (!advance || *advance != 42 || !decoded ||
      decoded->observation.element_id != 7 ||
      decoded->observation.source_url != envelope.observation.source_url ||
      media_ipc::ReadAdvanceMessage(media_ipc::CreateAdvanceMessage(0))) {
    return false;
  }
  auto malformed = media_ipc::CreateObservationMessage(envelope);
  malformed->GetArgumentList()->SetString(1, "7");
  if (media_ipc::ReadObservationMessage(malformed))
    return false;
  malformed = media_ipc::CreateObservationMessage(envelope);
  malformed->GetArgumentList()->SetInt(2, 99);
  if (media_ipc::ReadObservationMessage(malformed))
    return false;
  malformed = media_ipc::CreateObservationMessage(envelope);
  malformed->GetArgumentList()->SetDouble(5, 1.5);
  return !media_ipc::ReadObservationMessage(malformed);
}

NSButton* FindButton(NSView* view,
                     NSString* accessibility_label,
                     NSString* title) {
  if ([view isKindOfClass:[NSButton class]]) {
    NSButton* button = static_cast<NSButton*>(view);
    if ((accessibility_label &&
         [button.accessibilityLabel isEqualToString:accessibility_label]) ||
        (title && [button.title isEqualToString:title])) {
      return button;
    }
  }
  for (NSView* child in view.subviews) {
    if (NSButton* button = FindButton(child, accessibility_label, title))
      return button;
  }
  return nil;
}

// Test-only Browser process; Renderer execution uses the product Helper bundle
// and Markdown execution uses the product Rust content-host binary.
class SnapshotFixtureApp final : public CefApp,
                                 public CefBrowserProcessHandler {
 public:
  SnapshotFixtureApp(std::string fixture_url, std::string scenario)
      : fixture_url_(std::move(fixture_url)),
        recovery_url_(SiblingUrl(fixture_url_, "recovery.html")),
        scenario_(std::move(scenario)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  CefRefPtr<CefClient> GetDefaultClient() override {
    return controller_ ? controller_->client() : nullptr;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    static_cast<void>(process_type);
    command_line->AppendSwitch("use-mock-keychain");
    command_line->AppendSwitchWithValue("enable-logging", "stderr");
    if (scenario_ == "media" || IsAutomatedMediaScenario(scenario_)) {
      // Test-only: make playback deterministic without synthesizing a page
      // click. Product eligibility still requires the explicit trusted-input
      // fact below; the forged scenario runs without this switch/input.
      command_line->AppendSwitchWithValue("autoplay-policy",
                                          "no-user-gesture-required");
    }
  }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    if (!ValidateMediaIpcContracts()) {
      Finish(false, "media IPC contract rejected");
      return;
    }
    auto process = std::make_unique<ContentHostProcess>();
    process_ = process.get();
    content_host_ = std::make_unique<ContentHostAdapter>(std::move(process));
    auto media_process = std::make_unique<MediaHostProcess>();
    media_process_ = media_process.get();
    media_host_ = std::make_unique<MediaHostAdapter>(std::move(media_process));
    cast_shell_ = std::make_unique<CastShellController>(CastCommandPort{
        [this](auto action) { return media_host_->RequestDiscovery(action); },
        [this](auto revision, auto offset) {
          return media_host_->RequestDevicePage(revision, offset);
        },
        [this](auto candidate, auto device, auto handoff) {
          return media_host_->RequestStartCast(candidate, std::move(device),
                                               handoff);
        },
        [this](auto generation) {
          return media_host_->RequestStopCast(generation);
        }});
    cast_chrome_ = std::make_unique<CastChromeMac>(
        CastChromeStrings{"Choose cast device", "Stop casting",
                          "Cast to device", "No devices", "Cast", "Refresh",
                          "Cancel"},
        CastChromeCallbacks{
            [this] { return cast_shell_->ActivateCastButton(); },
            [this] { return cast_shell_->RefreshReceivers(); },
            [this] { cast_shell_->CancelReceiverPicker(); },
            [this](const std::string& device_id) {
              return cast_shell_->SelectReceiver(device_id);
            }});
    controller_ =
        new TabController(fixture_url_, [this](CefRefPtr<CefBrowser> browser) {
          browser_ = browser;
          if (scenario_ == "media-cast-ui") {
            static_cast<void>(cast_chrome_->AttachWindow(
                browser->GetIdentifier(),
                browser->GetHost()->GetWindowHandle()));
            cast_chrome_->SetActiveWindow(browser->GetIdentifier());
          }
        });
    if (scenario_ == "media-manual") {
      trusted_input_monitor_ = std::make_unique<TrustedInputMonitor>();
      if (!trusted_input_monitor_->Start(
              [this] { controller_->NoteTrustedUserInputForActiveTab(); })) {
        Finish(false, "trusted input monitor start rejected");
        return;
      }
    }
    controller_->SetPageSnapshotObserver(content_host_.get());
    controller_->SetPageSnapshotAdmission(
        [this] { return content_host_->healthy(); });
    controller_->SetPageSnapshotEventsReadyCallback(
        [this] { OnSnapshotEventsReady(); });
    controller_->SetMediaObservationEventsReadyCallback(
        [this] { ConsumeMediaEvents(); });
    controller_->SetMediaObservationLifecycleCallback(
        [this](std::uint32_t tab_id, std::uint64_t navigation_id,
               std::uint32_t generation, bool closed) {
          if (closed) {
            static_cast<void>(media_host_->CloseTab(tab_id, generation));
            cast_shell_->OnPageClosed();
          } else {
            static_cast<void>(media_host_->AdvanceNavigation(
                tab_id, navigation_id, generation));
            cast_shell_->OnNavigation();
          }
        });
    controller_->SetPageLoadCompletedCallback(
        [this](CefRefPtr<CefBrowser> browser) { OnPageLoaded(browser); });
    controller_->SetBrowsersClosedCallback([this] {
      tick_active_ = false;
      browser_ = nullptr;
      if (trusted_input_monitor_)
        trusted_input_monitor_->Stop();
      cast_shell_->Shutdown();
      cast_chrome_->Close();
      content_host_->Stop();
      media_host_->Stop();
      if (scenario_ == "close" && close_requested_) {
        passed_ = true;
        std::cout << "snapshot_fixture scenario=close terminal=completed"
                     " detail=closed without late Markdown markdown_bytes=0"
                     " mock_keychain=1"
                  << std::endl;
      }
    });
    if (!content_host_->Start(CRAYON_CONTENT_HOST_TEST_PATH) ||
        !media_host_->Start(CRAYON_MEDIA_HOST_TEST_PATH)) {
      Finish(false, "content/media host start rejected");
      return;
    }
    ScheduleStartupCheck();
  }

  bool passed() const { return passed_; }

 private:
  void ScheduleStartupCheck() {
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(
                           base::BindOnce(&SnapshotFixtureApp::ContinueStartup,
                                                           CefRefPtr<SnapshotFixtureApp>(this))),
                       kTickMilliseconds);
  }

  void ContinueStartup() {
    CEF_REQUIRE_UI_THREAD();
    if (content_host_->healthy() && media_host_->healthy()) {
      if (!controller_->CreateMainWindow()) {
        Finish(false, "browser creation rejected");
        return;
      }
      tick_active_ = true;
      ScheduleTick();
      return;
    }
    if (++startup_checks_ >= kStartupChecks) {
      Finish(false, "content-host health timeout");
      return;
    }
    ScheduleStartupCheck();
  }

  void ScheduleTick() {
    CefPostDelayedTask(
        TID_UI,
        CefCreateClosureTask(base::BindOnce(
            &SnapshotFixtureApp::Tick, CefRefPtr<SnapshotFixtureApp>(this))),
                       kTickMilliseconds);
  }

  void Tick() {
    CEF_REQUIRE_UI_THREAD();
    if (!tick_active_)
      return;
    const auto tick_now = std::chrono::steady_clock::now();
    if (last_tick_) {
      max_tick_delay_ =
          std::max(max_tick_delay_,
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       tick_now - *last_tick_));
    }
    last_tick_ = tick_now;
    if (scenario_ != "backpressure" || backpressure_released_) {
      ConsumeGatewayEvents();
    }
    content_host_->Tick();
    media_host_->Tick();
    if (media_host_crash_triggered_ &&
        media_process_->generation() > media_host_generation_before_crash_) {
      saw_media_host_recovery_ = true;
    }
    ConsumeMediaEvents();
    ConsumeMediaPlanningEvents();
    cast_shell_->ConsumeCast(media_host_->DrainCast(64));
    if (scenario_ == "media-cast-ui" && browser_) {
      static_cast<void>(cast_chrome_->AttachWindow(
          browser_->GetIdentifier(), browser_->GetHost()->GetWindowHandle()));
      cast_chrome_->SetActiveWindow(browser_->GetIdentifier());
    }
    cast_chrome_->Render(cast_shell_->coordinator());
    if (scenario_ == "media-cast-ui" && AdvanceCastUiScenario())
      return;
    ConsumeReplies();
    AdvanceCrashRecovery();
    if (media_checks_ > 0 && --media_checks_ == 0) {
      if (scenario_ == "media" || scenario_ == "media-manual" ||
          scenario_ == "media-clear-mp4") {
        Finish(saw_actual_media_ && saw_media_network_ && saw_http_media_ &&
                   saw_media_candidate_ && saw_media_decision_,
               "CEF clear media observation and trusted playback proof");
      } else if (scenario_ == "media-hls") {
        Finish(saw_actual_media_ && saw_manifest_network_ &&
                   saw_media_candidate_ && saw_media_decision_,
               "CEF HLS media and manifest observation");
      } else if (scenario_ == "media-dash") {
        Finish(saw_actual_media_ && saw_dash_network_ && saw_media_candidate_ &&
                   saw_media_decision_,
               "CEF DASH manifest reached media planning path");
      } else if (scenario_ == "media-credential") {
        Finish(saw_actual_media_ && saw_credential_network_ &&
                   saw_media_candidate_ && saw_media_decision_,
               "CEF credential class reached fail-closed planning path");
      } else if (scenario_ == "media-host-crash") {
        Finish(saw_media_candidate_before_crash_ &&
                   saw_media_candidate_after_crash_ &&
                   saw_media_host_recovery_ && saw_media_decision_,
               "media-host crash generation recovered without stale state");
      } else if (scenario_ == "media-blob" || scenario_ == "media-mse") {
        Finish(saw_actual_media_ && saw_blob_media_ && saw_media_decision_,
               "CEF URL-less blob/MSE media observation");
      } else if (scenario_ == "media-eme") {
        Finish(saw_actual_media_ && saw_protected_media_ &&
                   saw_media_candidate_ && saw_media_decision_ &&
                   saw_drm_reject_,
               "CEF EME protection marker observation");
      } else if (scenario_ == "media-ad") {
        Finish(saw_actual_media_ && saw_ad_media_ && saw_media_candidate_ &&
                   saw_media_decision_,
               "CEF ad-labelled media remained observable");
      } else if (scenario_ == "media-hidden" ||
                 scenario_ == "media-cross-frame") {
        Finish(!saw_media_, "hidden/cross-frame media remained ineligible");
      } else if (scenario_ == "media-forged") {
        Finish(!saw_media_, "forged page playback remained ineligible");
      } else if (scenario_ == "media-cast-ui") {
        Finish(false, "CEF cast chrome scenario timed out");
      }
      return;
    }
    if (no_reply_checks_ > 0 && --no_reply_checks_ == 0) {
      Finish(!unexpected_reply_, "no late Markdown reply");
      return;
    }
    ScheduleTick();
  }

  void OnPageLoaded(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (finished_ || !browser || !browser->GetMainFrame())
      return;
    const std::string loaded_url = browser->GetMainFrame()->GetURL();
    if (loaded_url == recovery_url_) {
      if (scenario_ == "media-cast-ui")
        cast_ui_navigated_ = true;
      if ((scenario_ == "navigation" || scenario_ == "crash") &&
          recovery_requested_ && !recovery_started_) {
        recovery_started_ = true;
        StartSnapshot(browser);
      }
      return;
    }
    if (loaded_url != fixture_url_ || initial_started_)
      return;
    initial_started_ = true;
    if (scenario_ == "media" || scenario_ == "media-manual" ||
        scenario_ == "media-forged" || IsAutomatedMediaScenario(scenario_)) {
      media_checks_ = scenario_ == "media-manual" ? 1500 : 250;
      if (scenario_ == "media" || IsAutomatedMediaScenario(scenario_)) {
        controller_->NoteTrustedUserInputForActiveTab();
        std::string script =
            "const audio = document.querySelector('audio,video');"
                             "if (audio) { audio.muted = true; audio.play(); }";
        if (scenario_ == "media-blob") {
          script =
              "fetch('/clear.mp4').then(r=>r.blob()).then(b=>{"
                   "const a=document.querySelector('audio,video');"
                   "a.src=URL.createObjectURL(b);a.muted=true;return a.play();});";
        } else if (scenario_ == "media-mse") {
          script =
              "const a=document.querySelector('audio,video');const m=new "
                   "MediaSource();"
                   "a.src=URL.createObjectURL(m);a.muted=true;"
                   "m.addEventListener('sourceopen',async()=>{"
                   "const b=m.addSourceBuffer('video/mp4; "
                   "codecs=\"av01.0.04M.08\"');"
                   "const d=await (await fetch('/mse.mp4')).arrayBuffer();"
                   "b.addEventListener('updateend',()=>{m.endOfStream();a.play();},{"
                   "once:true});"
                   "b.appendBuffer(d);},{once:true});";
        } else if (scenario_ == "media-eme") {
          script += "setTimeout(()=>audio.dispatchEvent(new "
                    "MediaEncryptedEvent('encrypted',"
                    "{initDataType:'cenc',initData:new ArrayBuffer(0)})),100);";
        } else if (scenario_ == "media-cross-frame") {
          script.clear();
        }
        if (!script.empty()) {
          browser->GetMainFrame()->ExecuteJavaScript(script, loaded_url, 1);
        }
      }
      return;
    }
    StartSnapshot(browser);
    if (!active_request_)
      return;

    if (scenario_ == "cancel") {
      const auto result = controller_->CancelPageSnapshot(*active_request_);
      if (result !=
          crayon::cef_shell::gateway::SnapshotGatewayResult::kAccepted) {
        Finish(false, "cancel rejected");
        return;
      }
      abandoned_request_ = active_request_->value();
      active_request_.reset();
      no_reply_checks_ = kNoReplyChecks;
    } else if (scenario_ == "navigation") {
      abandoned_request_ = active_request_->value();
      active_request_.reset();
      recovery_requested_ = true;
      browser->GetMainFrame()->LoadURL(recovery_url_);
    } else if (scenario_ == "close") {
      abandoned_request_ = active_request_->value();
      active_request_.reset();
      close_requested_ = true;
      controller_->CloseAllBrowsers(true);
    }
  }

  void StartSnapshot(CefRefPtr<CefBrowser> browser) {
    snapshot_started_ = std::chrono::steady_clock::now();
    first_chunk_elapsed_.reset();
    active_request_ = controller_->StartPageSnapshot(browser);
    expected_sequence_ = 0;
    events_ready_count_ = 0;
    markdown_.clear();
    if (!active_request_)
      Finish(false, "snapshot request rejected");
  }

  void OnSnapshotEventsReady() {
    CEF_REQUIRE_UI_THREAD();
    if (finished_)
      return;
    ++events_ready_count_;
    if (scenario_ == "backpressure" && !backpressure_released_) {
      if (events_ready_count_ >=
          crayon::cef_shell::gateway::kMaxQueuedSnapshotEvents) {
        backpressure_released_ = true;
        ConsumeGatewayEvents();
        if (!saw_capacity_terminal_) {
          Finish(false, "backpressure terminal missing");
          return;
        }
        abandoned_request_ = active_request_ ? active_request_->value() : "";
        active_request_.reset();
        no_reply_checks_ = kNoReplyChecks;
      }
      return;
    }
    ConsumeGatewayEvents();
    if (scenario_ == "crash" && !crash_triggered_ && active_request_) {
      crash_triggered_ = process_->Enqueue(host::Shutdown{});
      if (!crash_triggered_) {
        Finish(false, "Core crash trigger rejected");
      } else {
        abandoned_request_ = active_request_->value();
        active_request_.reset();
      }
    }
  }

  void ConsumeGatewayEvents() {
    std::vector<SnapshotGatewayEvent> events = controller_->DrainPageSnapshots(16);
    for (const auto &event : events) {
      const auto *terminal = std::get_if<SnapshotTerminal>(&event);
      if (terminal && terminal->status == SnapshotTerminalStatus::kRejected &&
          terminal->error == EngineErrorCode::kCapacityExceeded) {
        saw_capacity_terminal_ = true;
      }
    }
    content_host_->Consume(std::move(events));
  }

  void ConsumeMediaEvents() {
    if (!controller_) return;
    std::vector<BrowserMediaFact> facts;
    for (GatewayEvent &event : controller_->DrainMediaObservations(64)) {
      if (event.source == EventSource::kMedia) {
        cast_shell_->OnBrowserVerifiedMedia();
        saw_media_ = true;
        saw_actual_media_ = true;
        saw_http_media_ =
            saw_http_media_ ||
            event.media.source_kind == crayon::cef_shell::renderer::MediaSourceKind::kHttpUrl;
        saw_blob_media_ =
            saw_blob_media_ ||
            event.media.source_kind == crayon::cef_shell::renderer::MediaSourceKind::kBlobUrl;
        saw_ad_media_ =
            saw_ad_media_ || event.media.source_url.find("ad-clear.mp4") != std::string::npos;
      } else {
        saw_media_network_ = saw_media_network_ ||
                             event.network.url.find("clear.mp4") != std::string::npos ||
                             event.network.url.find("tone.wav") != std::string::npos;
        saw_manifest_network_ =
            saw_manifest_network_ ||
            event.network.kind == crayon::cef_shell::network::ResourceKind::kManifest;
        saw_dash_network_ =
            saw_dash_network_ ||
            event.network.url.find("clear.mpd") != std::string::npos;
        saw_credential_network_ =
            saw_credential_network_ ||
            event.network.header_class ==
                crayon::cef_shell::network::HeaderClass::kAuthorization;
        saw_protected_media_ =
            saw_protected_media_ || event.network.eme_encrypted;
      }
      auto page_url =
          controller_->TrustedPageUrl(event.tab_id, event.navigation_id);
      if (page_url) {
        facts.push_back(BrowserMediaFact{std::move(event), std::move(*page_url),
                                         MonotonicMilliseconds()});
      }
    }
    media_host_->Consume(std::move(facts));
  }

  void ConsumeMediaPlanningEvents() {
    static_cast<void>(media_host_->Drain(64));
    auto events = media_host_->DrainPlanning(64);
    cast_shell_->ConsumePlanning(events);
    for (auto& event : events) {
      if (event.kind == MediaPlanningEventKind::kCandidate &&
          event.candidate_id) {
        saw_media_candidate_ = true;
        if (scenario_ == "media-host-crash" && !media_host_crash_triggered_) {
          saw_media_candidate_before_crash_ = true;
          media_host_generation_before_crash_ = media_process_->generation();
          media_host_crash_triggered_ = media_process_->Enqueue(
              crayon::browser::cef_shell::macos::media_host_ipc::Shutdown{});
          continue;
        }
        if (scenario_ == "media-host-crash" && saw_media_host_recovery_) {
          saw_media_candidate_after_crash_ = true;
        }
        if (scenario_ == "media-eme" && !saw_protected_media_)
          continue;
        if (!decision_requested_) {
          decision_requested_ = media_host_->Submit(
              crayon::browser::cef_shell::macos::media_host_ipc::Decide{
                  "fixture-decision",
                  *event.candidate_id,
                  MonotonicMilliseconds(),
                  {true, true, true, true, true, true, 4320},
                  false});
        }
      } else if (event.kind == MediaPlanningEventKind::kDecision) {
        saw_media_decision_ = true;
        saw_drm_reject_ = saw_drm_reject_ ||
            (event.decision &&
             event.decision->reject_reason ==
                               crayon::browser::cef_shell::macos::
                                   media_host_ipc::CoreError::kDrmProtected);
      }
    }
  }

  bool AdvanceCastUiScenario() {
    if (!browser_ || finished_)
      return false;
    NSView* view = CAST_CEF_WINDOW_HANDLE_TO_NSVIEW(
        browser_->GetHost()->GetWindowHandle());
    NSWindow* window = view.window;
    if (!window)
      return false;
    NSButton* button =
        FindButton(window.contentView, @"Choose cast device", nil);
    if (!button) {
      for (NSTitlebarAccessoryViewController* accessory in window
               .titlebarAccessoryViewControllers) {
        button = FindButton(accessory.view, @"Choose cast device", nil);
        if (button)
          break;
      }
    }
    if (!cast_ui_opened_ && button && !button.hidden && button.enabled) {
      [button performClick:nil];
      cast_ui_opened_ = true;
      return false;
    }
    if (cast_ui_opened_ && !cast_ui_cancelled_ &&
        !cast_shell_->device_page_pending() && window.attachedSheet) {
      NSButton* select =
          FindButton(window.attachedSheet.contentView, nil, @"Cast");
      NSButton* cancel =
          FindButton(window.attachedSheet.contentView, nil, @"Cancel");
      if (!select || select.enabled || !cancel)
        return false;
      [cancel performClick:nil];
      cast_ui_cancelled_ = true;
      browser_->GetMainFrame()->LoadURL(recovery_url_);
      return false;
      }
    if (cast_ui_cancelled_ && cast_ui_navigated_ && button && button.hidden &&
        !window.attachedSheet) {
      Finish(saw_actual_media_ && saw_media_candidate_,
             "CEF cast chrome empty picker cancel and navigation cleanup");
      return true;
    }
    return false;
  }

  void ConsumeReplies() {
    for (host::Message &message : content_host_->Drain(64)) {
      const auto *chunk = std::get_if<host::MarkdownChunk>(&message);
      if (!chunk) {
        const auto *error = std::get_if<host::ErrorReply>(&message);
        if (scenario_ == "crash" && error && error->request_id == abandoned_request_) {
          continue;
        }
        Finish(false, "content-host returned error");
        return;
      }
      if (!active_request_ || chunk->request_id != active_request_->value() ||
          chunk->sequence != expected_sequence_++) {
        unexpected_reply_ = true;
        Finish(false, "stale or out-of-order reply");
        return;
      }
      if (!first_chunk_elapsed_ && snapshot_started_) {
        first_chunk_elapsed_ =
            std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - *snapshot_started_);
      }
      markdown_ += chunk->markdown;
      if (chunk->completed) {
        ValidateCompletedMarkdown();
        return;
      }
    }
  }

  void ValidateCompletedMarkdown() {
    if (scenario_ == "empty") {
      Finish(markdown_.empty(), "empty Markdown");
      return;
    }
    if (scenario_ == "security") {
      const bool includes_public =
          markdown_.find("# Security fixture heading") != std::string::npos &&
          markdown_.find("Visible security content") != std::string::npos;
      const bool excludes_private =
          markdown_.find("hidden-security-secret") == std::string::npos &&
          markdown_.find("aria-security-secret") == std::string::npos &&
          markdown_.find("styled-security-secret") == std::string::npos &&
          markdown_.find("password-security-secret") == std::string::npos &&
          markdown_.find("frame-security-secret") == std::string::npos;
      Finish(includes_public && excludes_private,
             "hidden sensitive and child-frame content excluded");
      return;
    }
    if (scenario_ == "perf") {
      Finish(markdown_.size() >= 100 * 1024,
             "100KiB deterministic Markdown performance fixture");
      return;
    }
    const bool recovery = scenario_ == "navigation" || scenario_ == "crash";
    const bool has_expected_heading =
        recovery
            ? markdown_.find("# Recovery fixture heading") != std::string::npos
                 : markdown_.find("# Visible fixture heading") != std::string::npos;
    const bool has_table =
        recovery || markdown_.find("| Name | Value |") != std::string::npos;
    const bool hides_secret =
        markdown_.find("hidden fixture secret") == std::string::npos;
    const bool no_abandoned =
        abandoned_request_.empty() ||
                              (active_request_ && active_request_->value() != abandoned_request_);
    Finish(has_expected_heading && has_table && hides_secret && no_abandoned,
           "deterministic Markdown");
  }

  void AdvanceCrashRecovery() {
    if (scenario_ != "crash" || !crash_triggered_ || recovery_requested_) {
      return;
    }
    if (!process_->healthy()) {
      saw_core_unhealthy_ = true;
      active_request_.reset();
      return;
    }
    if (saw_core_unhealthy_) {
      recovery_requested_ = true;
      browser_->GetMainFrame()->LoadURL(recovery_url_);
    }
  }

  void Finish(bool passed, const char *detail) {
    if (finished_) return;
    finished_ = true;
    passed_ = passed;
    tick_active_ = false;
    const auto media_diagnostics =
        controller_ ? controller_->media_observation_diagnostics()
                    : crayon::browser::cef_shell::observation::MediaObservationDiagnostics{};
    const auto complete_time = snapshot_started_
                                   ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - *snapshot_started_)
                                         .count()
                                   : 0;
    std::cout << "snapshot_fixture scenario=" << scenario_
              << " terminal=" << (passed ? "completed" : "failed") << " detail=" << detail
              << " markdown_bytes=" << markdown_.size() << " media=" << (saw_media_ ? 1 : 0)
              << " actual_media=" << (saw_actual_media_ ? 1 : 0)
              << " media_network=" << (saw_media_network_ ? 1 : 0) << " mock_keychain=1"
              << " media_received=" << media_diagnostics.received_total
              << " media_current=" << media_diagnostics.accepted_current_total
              << " media_denied=" << media_diagnostics.proof_denied_total
              << " media_eligible=" << media_diagnostics.eligible_total
              << " first_chunk_ms="
              << (first_chunk_elapsed_ ? first_chunk_elapsed_->count() : 0)
              << " complete_ms=" << complete_time
              << " max_tick_delay_ms=" << max_tick_delay_.count() << std::endl;
    if (controller_) {
      controller_->CloseAllBrowsers(true);
    } else {
      if (content_host_)
        content_host_->Stop();
      if (media_host_)
        media_host_->Stop();
      CefQuitMessageLoop();
    }
  }

  const std::string fixture_url_;
  const std::string recovery_url_;
  const std::string scenario_;
  CefRefPtr<TabController> controller_;
  CefRefPtr<CefBrowser> browser_;
  std::unique_ptr<ContentHostAdapter> content_host_;
  std::unique_ptr<MediaHostAdapter> media_host_;
  std::unique_ptr<CastShellController> cast_shell_;
  std::unique_ptr<CastChromeMac> cast_chrome_;
  std::unique_ptr<TrustedInputMonitor> trusted_input_monitor_;
  ContentHostTransport *process_ = nullptr;
  MediaHostProcess *media_process_ = nullptr;
  std::optional<SnapshotRequestId> active_request_;
  std::string abandoned_request_;
  std::string markdown_;
  std::uint32_t expected_sequence_ = 0;
  std::size_t startup_checks_ = 0;
  std::size_t events_ready_count_ = 0;
  std::size_t no_reply_checks_ = 0;
  std::size_t media_checks_ = 0;
  std::optional<std::chrono::steady_clock::time_point> snapshot_started_;
  std::optional<std::chrono::milliseconds> first_chunk_elapsed_;
  std::optional<std::chrono::steady_clock::time_point> last_tick_;
  std::chrono::milliseconds max_tick_delay_{0};
  bool tick_active_ = false;
  bool initial_started_ = false;
  bool recovery_requested_ = false;
  bool recovery_started_ = false;
  bool crash_triggered_ = false;
  bool saw_core_unhealthy_ = false;
  bool backpressure_released_ = false;
  bool saw_capacity_terminal_ = false;
  bool unexpected_reply_ = false;
  bool saw_media_ = false;
  bool saw_actual_media_ = false;
  bool saw_media_network_ = false;
  bool saw_http_media_ = false;
  bool saw_blob_media_ = false;
  bool saw_manifest_network_ = false;
  bool saw_dash_network_ = false;
  bool saw_credential_network_ = false;
  bool saw_protected_media_ = false;
  bool saw_ad_media_ = false;
  bool saw_media_candidate_ = false;
  bool saw_media_decision_ = false;
  bool decision_requested_ = false;
  bool saw_drm_reject_ = false;
  bool media_host_crash_triggered_ = false;
  bool saw_media_host_recovery_ = false;
  bool saw_media_candidate_before_crash_ = false;
  bool saw_media_candidate_after_crash_ = false;
  std::uint64_t media_host_generation_before_crash_ = 0;
  bool close_requested_ = false;
  bool cast_ui_opened_ = false;
  bool cast_ui_cancelled_ = false;
  bool cast_ui_navigated_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(SnapshotFixtureApp);
  DISALLOW_COPY_AND_ASSIGN(SnapshotFixtureApp);
};

}  // namespace

@interface SnapshotTestApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handling_send_event_;
}
@end

@implementation SnapshotTestApplication
- (BOOL)isHandlingSendEvent {
  return handling_send_event_;
}
- (void)setHandlingSendEvent:(BOOL)value {
  handling_send_event_ = value;
}
- (void)sendEvent:(NSEvent *)event {
  CefScopedSendingEvent sending_event_scope;
  [super sendEvent:event];
}
@end

int main(int argc, char *argv[]) {
  if (argc != 3) return 2;
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) return 3;
  @autoreleasepool {
    [SnapshotTestApplication sharedApplication];
    CefMainArgs main_args(argc, argv);
    CefSettings settings;
    settings.no_sandbox = true;
    settings.log_severity = LOGSEVERITY_WARNING;
    const std::filesystem::path cache_path =
        std::filesystem::temp_directory_path() /
        ("crayon-page-snapshot-integration-" + std::to_string(getpid()));
    CefString(&settings.root_cache_path).FromString(cache_path.string());
    CefString(&settings.browser_subprocess_path)
        .FromString(CRAYON_SNAPSHOT_TEST_HELPER_PATH);
    CefRefPtr<SnapshotFixtureApp> app(new SnapshotFixtureApp(argv[1], argv[2]));
    if (!CefInitialize(main_args, settings, app, nullptr))
      return 4;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    [NSApp activateIgnoringOtherApps:YES];
    CefRunMessageLoop();
    const bool passed = app->passed();
    CefShutdown();
    std::error_code cleanup_error;
    std::filesystem::remove_all(cache_path, cleanup_error);
    return passed ? 0 : 1;
  }
}
