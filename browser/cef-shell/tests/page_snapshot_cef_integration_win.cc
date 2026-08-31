// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

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

#include "browser/new_tab/cef_new_tab_handler.h"
#include "browser/window/tab_controller.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_task.h"
#include "include/cef_version_info.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "windows/content_host_adapter_win.h"

#ifndef CRAYON_CONTENT_HOST_TEST_PATH
#error "CRAYON_CONTENT_HOST_TEST_PATH must be defined"
#endif

namespace {

namespace host = crayon::browser::cef_shell::windows::content_host_ipc;
using crayon::browser::cef_shell::window::TabController;
using crayon::browser::cef_shell::windows::ContentHostAdapter;
using crayon::browser::cef_shell::windows::ContentHostProcess;
using crayon::browser_engine::EngineErrorCode;
using crayon::browser_engine::SnapshotRequestId;
using crayon::browser_engine::SnapshotTerminal;
using crayon::browser_engine::SnapshotTerminalStatus;
using crayon::cef_shell::gateway::SnapshotGatewayEvent;

constexpr std::int64_t kTickMilliseconds = 20;
constexpr std::size_t kStartupChecks = 500;
constexpr std::size_t kNoReplyChecks = 25;

std::string WideToUtf8(const wchar_t* value) {
  if (!value || !*value) return {};
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                           -1, nullptr, 0, nullptr, nullptr);
  if (required <= 1) return {};
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                          result.data(), required, nullptr,
                          nullptr) != required) {
    return {};
  }
  result.pop_back();
  return result;
}

std::string SiblingUrl(const std::string& url, const char* filename) {
  const std::size_t slash = url.rfind('/');
  return slash == std::string::npos ? std::string{}
                                    : url.substr(0, slash + 1) + filename;
}

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
    command_line->AppendSwitchWithValue("enable-logging", "stderr");
    command_line->AppendSwitch("disable-background-networking");
    command_line->AppendSwitch("disable-component-update");
    command_line->AppendSwitch("disable-default-apps");
    command_line->AppendSwitch("disable-sync");
    command_line->AppendSwitch("metrics-recording-only");
    command_line->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    auto process = std::make_unique<ContentHostProcess>();
    process_ = process.get();
    content_host_ = std::make_unique<ContentHostAdapter>(std::move(process));
    controller_ = new TabController(
        fixture_url_,
        [this](CefRefPtr<CefBrowser> browser) { browser_ = browser; });
    controller_->SetPageSnapshotObserver(content_host_.get());
    controller_->SetPageSnapshotAdmission(
        [this] { return content_host_->healthy(); });
    controller_->SetPageSnapshotEventsReadyCallback(
        [this] { OnSnapshotEventsReady(); });
    controller_->SetPageLoadCompletedCallback(
        [this](CefRefPtr<CefBrowser> browser) { OnPageLoaded(browser); });
    controller_->SetBrowsersClosedCallback([this] {
      tick_active_ = false;
      browser_ = nullptr;
      content_host_->Stop();
      if (scenario_ == "close" && close_requested_) {
        passed_ = true;
        std::cout << "snapshot_fixture platform=windows scenario=close"
                     " terminal=completed detail=closed without late Markdown"
                     " markdown_bytes=0"
                  << std::endl;
      }
    });
    if (!content_host_->Start(CRAYON_CONTENT_HOST_TEST_PATH)) {
      Finish(false, "content-host start rejected");
      return;
    }
    ScheduleStartupCheck();
  }

  bool passed() const { return passed_; }

  void PrepareForShutdown() {
    CEF_REQUIRE_UI_THREAD();
    browser_ = nullptr;
    controller_ = nullptr;
    content_host_.reset();
    process_ = nullptr;
  }

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
    if (content_host_->healthy()) {
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
    if (!tick_active_) return;
    const auto now = std::chrono::steady_clock::now();
    if (last_tick_) {
      max_tick_delay_ =
          std::max(max_tick_delay_,
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       now - *last_tick_));
    }
    last_tick_ = now;
    if (scenario_ != "backpressure" || backpressure_released_) {
      ConsumeGatewayEvents();
    }
    content_host_->Tick();
    ConsumeReplies();
    AdvanceCrashRecovery();
    if (no_reply_checks_ > 0 && --no_reply_checks_ == 0) {
      Finish(!unexpected_reply_, "no late Markdown reply");
      return;
    }
    ScheduleTick();
  }

  void OnPageLoaded(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (finished_ || !browser || !browser->GetMainFrame()) return;
    const std::string loaded_url = browser->GetMainFrame()->GetURL();
    if (loaded_url == recovery_url_) {
      if ((scenario_ == "navigation" || scenario_ == "crash") &&
          recovery_requested_ && !recovery_started_) {
        recovery_started_ = true;
        StartSnapshot(browser);
      }
      return;
    }
    if (loaded_url != fixture_url_ || initial_started_) return;
    initial_started_ = true;
    StartSnapshot(browser);
    if (!active_request_) return;

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
    if (!active_request_) Finish(false, "snapshot request rejected");
  }

  void OnSnapshotEventsReady() {
    CEF_REQUIRE_UI_THREAD();
    if (finished_) return;
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
    std::vector<SnapshotGatewayEvent> events =
        controller_->DrainPageSnapshots(16);
    for (const auto& event : events) {
      const auto* terminal = std::get_if<SnapshotTerminal>(&event);
      if (terminal && terminal->status == SnapshotTerminalStatus::kRejected &&
          terminal->error == EngineErrorCode::kCapacityExceeded) {
        saw_capacity_terminal_ = true;
      }
    }
    content_host_->Consume(std::move(events));
  }

  void ConsumeReplies() {
    for (host::Message& message : content_host_->Drain(64)) {
      const auto* chunk = std::get_if<host::MarkdownChunk>(&message);
      if (!chunk) {
        const auto* error = std::get_if<host::ErrorReply>(&message);
        if (scenario_ == "crash" && error &&
            error->request_id == abandoned_request_) {
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
      if (CefRefPtr<CefBrowser> browser = browser_) {
        browser->GetMainFrame()->LoadURL(recovery_url_);
      }
    }
  }

  void Finish(bool passed, const char* detail) {
    if (finished_) return;
    finished_ = true;
    passed_ = passed;
    tick_active_ = false;
    const auto complete_time =
        snapshot_started_
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - *snapshot_started_)
                  .count()
            : 0;
    std::cout << "snapshot_fixture platform=windows scenario=" << scenario_
              << " terminal=" << (passed ? "completed" : "failed")
              << " detail=" << detail << " markdown_bytes=" << markdown_.size()
              << " first_chunk_ms="
              << (first_chunk_elapsed_ ? first_chunk_elapsed_->count() : 0)
              << " complete_ms=" << complete_time
              << " max_tick_delay_ms=" << max_tick_delay_.count() << std::endl;
    if (controller_) {
      controller_->CloseAllBrowsers(true);
    } else {
      if (content_host_) content_host_->Stop();
      CefQuitMessageLoop();
    }
  }

  const std::string fixture_url_;
  const std::string recovery_url_;
  const std::string scenario_;
  CefRefPtr<TabController> controller_;
  CefRefPtr<CefBrowser> browser_;
  std::unique_ptr<ContentHostAdapter> content_host_;
  ContentHostProcess* process_ = nullptr;
  std::optional<SnapshotRequestId> active_request_;
  std::string abandoned_request_;
  std::string markdown_;
  std::uint32_t expected_sequence_ = 0;
  std::size_t startup_checks_ = 0;
  std::size_t events_ready_count_ = 0;
  std::size_t no_reply_checks_ = 0;
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
  bool close_requested_ = false;
  bool finished_ = false;
  bool passed_ = false;

  IMPLEMENT_REFCOUNTING(SnapshotFixtureApp);
  DISALLOW_COPY_AND_ASSIGN(SnapshotFixtureApp);
};

}  // namespace

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPTSTR command_line,
                                    int show_command, void* sandbox_info,
                                    cef_version_info_t* version_info) {
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);
  if (!version_info || !sandbox_info) return 2;

  CefMainArgs main_args(instance);
  const int child_exit_code = CefExecuteProcess(
      main_args, crayon::browser::cef_shell::new_tab::CreateNewTabProcessApp(),
      sandbox_info);
  if (child_exit_code >= 0) return child_exit_code;

  int argument_count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (!arguments || argument_count != 3) {
    if (arguments) LocalFree(arguments);
    return 2;
  }
  const std::string fixture_url = WideToUtf8(arguments[1]);
  const std::string scenario = WideToUtf8(arguments[2]);
  LocalFree(arguments);
  if (fixture_url.empty() || scenario.empty()) return 2;

  const std::filesystem::path cache_path =
      std::filesystem::temp_directory_path() /
      (L"crayon-page-snapshot-integration-" +
       std::to_wstring(GetCurrentProcessId()));
  CefSettings settings;
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.root_cache_path).FromWString(cache_path.wstring());
  CefRefPtr<SnapshotFixtureApp> app(
      new SnapshotFixtureApp(fixture_url, scenario));
  if (!CefInitialize(main_args, settings, app, sandbox_info)) return 4;
  CefRunMessageLoop();
  const bool passed = app->passed();
  app->PrepareForShutdown();
  app = nullptr;
  CefShutdown();
  return passed ? 0 : 1;
}
