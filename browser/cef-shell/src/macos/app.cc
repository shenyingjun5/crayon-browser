#include "macos/app.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/mdv/cef_mdv_handler.h"
#include "browser/new_tab/cef_new_tab_handler.h"
#include "browser/permission/permission_store.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "include/cef_color_ids.h"
#include "include/views/cef_browser_view.h"
#include "include/cef_id_mappers.h"
#include "browser/window/alloy_chrome_palette.h"
#include <fstream>
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "macos/agent_confirm_sheet_mac.h"
#include "macos/media_host_process_mac.h"
#include "macos/page_markdown_platform_mac.h"
#include "macos/trusted_input_monitor_mac.h"


namespace crayon::browser::cef_shell {

// --- AGT-12Cc2r: agent-host callback plumbing ---------------------------

constexpr char kAgentHostPurpose[] = "agent-caap";
constexpr char kAgentHostProfile[] = "default";
constexpr std::uint64_t kAgentHostGrantTtlMs = 600000;
// Only capabilities whose tools are really wired into the execute bridge
// are advertised; capability advertisement mirrors real assembly.
constexpr char kAgentHostCapabilityPageRead[] = "page_read";
// The serve thread blocks while the UI thread computes a callback result;
// every wait is bounded so shutdown cannot depend on UI responsiveness.
constexpr std::chrono::milliseconds kAgentUiWaitMs{2000};
constexpr std::chrono::milliseconds kAgentCancelPollMs{100};
constexpr std::size_t kMaxAgentFieldBytes = 2048;

// CAAP error indices (ffi.rs caap_error_at ordering); the FFI execute
// status is kAgentHostExecCaapError + index.
constexpr int kAgentErrTargetInvalid = 3;
constexpr int kAgentErrDeadlineExceeded = 6;
constexpr int kAgentErrInvalidMessage = 9;

// Shared serve-thread → UI-thread marshaling state. Defined at namespace
// scope (declared in app.h) so BrowserApp can own it ahead of the bridge
// member and the gate outlives the teardown that joins the serve thread.
struct AgentUiState final {
  enum class Work {
    kResolveActiveTab,
    kTabKnown,
    kExecuteTool,
  };
  struct Pending final {
    bool fulfilled = false;
    bool ok = false;
    std::string text;
  };
  enum class WaitOutcome {
    kFulfilled,
    kCancelled,
    kFailed,
  };

  std::mutex mu;
  std::condition_variable cv;
  bool shutting_down = false;
  std::uint64_t next_ticket = 1;
  std::map<std::uint64_t, Pending> pending;
  // UI-thread bindings installed once by StartAgentHost before the serve
  // thread exists, then only read on TID_UI while shutting_down is false.
  std::function<std::string()> resolve_active_tab;
  std::function<bool(const std::string& tab)> tab_known;
  std::function<std::optional<std::string>(const std::string& tool,
                                           const std::string& tab)>
      execute_tool;
  // AGT-05C: fire-and-forget connect event (client, capabilities CSV).
  std::function<void(const std::string&, const std::string&)> client_connected;

  // Serve-thread side: reserves a result slot. 0 means shutting down.
  std::uint64_t OpenTicket() {
    std::lock_guard<std::mutex> lock(mu);
    if (shutting_down) {
      return 0;
    }
    const std::uint64_t ticket = next_ticket++;
    pending.emplace(ticket, Pending{});
    return ticket;
  }

  // Serve-thread side: bounded wait. is_cancelled is only called without
  // holding mu.
  WaitOutcome WaitFor(std::uint64_t ticket, bool (*is_cancelled)(void* user),
                      void* cancel_user, Pending* out) {
    const auto deadline = std::chrono::steady_clock::now() + kAgentUiWaitMs;
    std::unique_lock<std::mutex> lock(mu);
    for (;;) {
      const auto now = std::chrono::steady_clock::now();
      if (shutting_down || now >= deadline) {
        break;
      }
      const auto it = pending.find(ticket);
      if (it == pending.end()) {
        break;
      }
      if (it->second.fulfilled) {
        *out = it->second;
        pending.erase(it);
        return WaitOutcome::kFulfilled;
      }
      lock.unlock();
      const bool cancelled = is_cancelled && is_cancelled(cancel_user);
      lock.lock();
      if (cancelled) {
        pending.erase(ticket);
        return WaitOutcome::kCancelled;
      }
      cv.wait_until(lock, std::min(now + kAgentCancelPollMs, deadline));
    }
    pending.erase(ticket);
    return WaitOutcome::kFailed;
  }

  // UI-thread side. Fulfillments for abandoned tickets no-op.
  void Fulfill(std::uint64_t ticket, bool ok, std::string text) {
    {
      std::lock_guard<std::mutex> lock(mu);
      if (shutting_down) {
        return;
      }
      const auto it = pending.find(ticket);
      if (it == pending.end() || it->second.fulfilled) {
        return;
      }
      it->second.ok = ok;
      it->second.text = std::move(text);
      it->second.fulfilled = true;
    }
    cv.notify_all();
  }

  // UI-thread side, called before the FFI stop joins the serve thread.
  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(mu);
      shutting_down = true;
    }
    cv.notify_all();
  }
};

namespace {

constexpr char kInitialUrl[] = "crayon://newtab";
constexpr char kOriginalSettingsUrl[] = "chrome://settings";
constexpr std::size_t kContentHostStartupChecks = 500;
constexpr std::int64_t kContentHostTickMilliseconds = 20;

std::string Utf8(CFStringRef value) {
  if (!value) return {};
  const CFIndex length = CFStringGetLength(value);
  const CFIndex capacity =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<std::size_t>(capacity));
  return CFStringGetCString(value, buffer.data(), capacity,
                            kCFStringEncodingUTF8)
             ? std::string(buffer.data())
             : std::string();
}

std::string HelperExecutablePath(const char* helper_name) {
  CFURLRef bundle_url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
  if (!bundle_url) return {};
  CFStringRef bundle_path =
      CFURLCopyFileSystemPath(bundle_url, kCFURLPOSIXPathStyle);
  CFRelease(bundle_url);
  const std::string path = Utf8(bundle_path);
  if (bundle_path) CFRelease(bundle_path);
  if (path.empty()) return {};
  return (std::filesystem::path(path) / "Contents" / "Helpers" / helper_name)
      .string();
}

std::uint64_t MonotonicMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

::crayon::browser::product_strings::ProductStrings BuildProductStringsOrEmpty(
    const ::crayon::browser::localization::LocaleSnapshot& snapshot) {
  return ::crayon::browser::product_strings::BuildProductStrings(
             snapshot, browser_mdv::MdvShortcutPlatform::kMacOS)
      .value_or(::crayon::browser::product_strings::ProductStrings{});
}

page_markdown::PageMarkdownStrings BuildPageMarkdownStrings(
    const ::crayon::browser::product_strings::PageMarkdownStrings& strings) {
  return page_markdown::PageMarkdownStrings{
      strings.preview_command,    strings.copy_command,
      strings.save_as_command,    strings.copied_status,
      strings.copy_failed_status, strings.save_cancelled_status};
}

// C20c: shared product strings → AppKit cast button/picker strings. The
// titlebar accessory renders these directly (UTF-8).
macos::CastChromeStrings BuildCastChromeStrings(
    const ::crayon::browser::product_strings::CastStrings& strings) {
  return macos::CastChromeStrings{
      strings.button_select,     strings.button_stop,
      strings.picker_title,      strings.picker_empty,
      strings.picker_select,     strings.picker_refresh,
      strings.picker_cancel,     strings.cast_code_label,
      strings.cast_code_connect, strings.cast_code_failed,
      strings.playback_pause,    strings.playback_resume,
      strings.playback_seek,     strings.playback_seconds,
      strings.playback_failed,   strings.rejected,
      strings.rejected_no_route, strings.rejected_drm,
      strings.retry,             strings.button_idle};
}

bool CastChromeStringsComplete(const macos::CastChromeStrings& strings) {
  return !strings.button_select.empty() && !strings.button_stop.empty() &&
         !strings.picker_title.empty() && !strings.picker_select.empty() &&
         !strings.picker_refresh.empty() && !strings.picker_cancel.empty() &&
         !strings.cast_code_label.empty() &&
         !strings.cast_code_connect.empty() &&
         !strings.cast_code_failed.empty() &&
         !strings.playback_pause.empty() && !strings.rejected.empty() &&
         !strings.button_idle.empty();
}

// Serve-thread trampoline helpers and the UI-thread runner for the CAAP
// agent host. The constants and AgentUiState live at the enclosing
// namespace scope (see app.h); everything here is TU-local.

void AgentUiRunWork(AgentUiState* state, std::uint64_t ticket,
                    AgentUiState::Work work, std::string tool,
                    std::string arg) {
  CEF_REQUIRE_UI_THREAD();
  {
    std::lock_guard<std::mutex> lock(state->mu);
    if (state->shutting_down) {
      return;
    }
  }
  bool ok = false;
  std::string text;
  switch (work) {
    case AgentUiState::Work::kResolveActiveTab:
      if (state->resolve_active_tab) {
        text = state->resolve_active_tab();
        ok = !text.empty();
      }
      break;
    case AgentUiState::Work::kTabKnown:
      if (state->tab_known) {
        ok = state->tab_known(arg);
        text = ok ? "1" : "0";
      }
      break;
    case AgentUiState::Work::kExecuteTool:
      if (state->execute_tool) {
        if (auto result = state->execute_tool(tool, arg)) {
          ok = true;
          text = std::move(*result);
        }
      }
      break;
  }
  state->Fulfill(ticket, ok, std::move(text));
}

bool PostAgentUiWork(AgentUiState* state, std::uint64_t ticket,
                     AgentUiState::Work work, std::string tool,
                     std::string arg) {
  return CefPostTask(
      TID_UI, base::BindOnce(&AgentUiRunWork, base::Unretained(state), ticket,
                             work, std::move(tool), std::move(arg)));
}

std::optional<std::uint64_t> ParseTabIdText(const std::string& text) {
  if (text.empty() || text.size() > 19) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    const int digit = c - '0';
    if (digit < 0 || digit > 9) {
      return std::nullopt;
    }
    // Checked math: a wrapping id would only lookup-miss, but an overflow
    // must not synthesize a different tab identity.
    if (value > (UINT64_MAX - static_cast<std::uint64_t>(digit)) / 10) {
      return std::nullopt;
    }
    value = value * 10 + static_cast<std::uint64_t>(digit);
  }
  return value;
}

std::string JsonEscape(const std::string& value) {
  static constexpr char kHex[] = "0123456789abcdef";
  const std::size_t limit = std::min(value.size(), kMaxAgentFieldBytes);
  std::string out;
  out.reserve(limit + 8);
  for (std::size_t index = 0; index < limit; ++index) {
    const char c = value[index];
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += "\\u00";
          out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
          out += kHex[static_cast<unsigned char>(c) & 0xF];
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Serve-thread trampolines (C linkage to match the Rust extern "C" ABI).

extern "C" const char* AgentResolveActiveTabTrampoline(void* user) {
  auto* state = static_cast<AgentUiState*>(user);
  const std::uint64_t ticket = state->OpenTicket();
  if (ticket == 0) {
    return nullptr;
  }
  if (!PostAgentUiWork(state, ticket, AgentUiState::Work::kResolveActiveTab,
                       std::string(), std::string())) {
    state->Fulfill(ticket, false, std::string());
  }
  AgentUiState::Pending out;
  if (state->WaitFor(ticket, nullptr, nullptr, &out) !=
          AgentUiState::WaitOutcome::kFulfilled ||
      !out.ok) {
    return nullptr;
  }
  return crayon_agent_host_string_alloc(out.text.c_str());
}

extern "C" int AgentTabKnownTrampoline(const char* tab, void* user) {
  if (!tab) {
    return 1;
  }
  auto* state = static_cast<AgentUiState*>(user);
  const std::uint64_t ticket = state->OpenTicket();
  if (ticket == 0) {
    return 1;
  }
  if (!PostAgentUiWork(state, ticket, AgentUiState::Work::kTabKnown,
                       std::string(), std::string(tab))) {
    state->Fulfill(ticket, false, std::string());
  }
  AgentUiState::Pending out;
  return state->WaitFor(ticket, nullptr, nullptr, &out) ==
                     AgentUiState::WaitOutcome::kFulfilled &&
                 out.ok
             ? 0
             : 1;
}

extern "C" CrayonAgentHostExecuteResult AgentExecuteTrampoline(
    const char* tool, const char* request_json, const char* tab,
    bool (*is_cancelled)(void* user), void* cancel_user, void* user) {
  // request_json is intentionally unused: the assembled read tools take no
  // parameters and parameter validation stays in the gateway.
  (void)request_json;
  static constexpr std::string_view kListTargets = "page.list_targets";
  static constexpr std::string_view kGetTitle = "page.get_title";
  const std::string_view tool_view =
      tool ? std::string_view(tool) : std::string_view();
  if (tool_view != kListTargets && tool_view != kGetTitle) {
    // Known to the registry but not assembled in this adapter: fail with a
    // stable CAAP error instead of pretending success.
    return {kAgentHostExecCaapError + kAgentErrInvalidMessage, nullptr};
  }
  auto* state = static_cast<AgentUiState*>(user);
  const std::uint64_t ticket = state->OpenTicket();
  if (ticket == 0) {
    return {kAgentHostExecCancelled, nullptr};
  }
  if (!PostAgentUiWork(state, ticket, AgentUiState::Work::kExecuteTool,
                       std::string(tool_view),
                       tab ? std::string(tab) : std::string())) {
    state->Fulfill(ticket, false, std::string());
  }
  AgentUiState::Pending out;
  switch (state->WaitFor(ticket, is_cancelled, cancel_user, &out)) {
    case AgentUiState::WaitOutcome::kFulfilled:
      if (out.ok) {
        return {kAgentHostExecOk,
                crayon_agent_host_string_alloc(out.text.c_str())};
      }
      return {kAgentHostExecCaapError + kAgentErrTargetInvalid, nullptr};
    case AgentUiState::WaitOutcome::kCancelled:
      return {kAgentHostExecCancelled, nullptr};
    case AgentUiState::WaitOutcome::kFailed:
      return {kAgentHostExecCaapError + kAgentErrDeadlineExceeded, nullptr};
  }
  return {kAgentHostExecCaapError + kAgentErrInvalidMessage, nullptr};
}

// AGT-05C: serve-thread → UI-thread connect event. Fire-and-forget; a
// dropped post (loop gone) is fine because shutdown revokes everything.
void AgentUiClientConnected(AgentUiState* state,
                            std::string client,
                            std::string capabilities) {
  CEF_REQUIRE_UI_THREAD();
  {
    std::lock_guard<std::mutex> lock(state->mu);
    if (state->shutting_down) {
      return;
    }
  }
  if (state->client_connected) {
    state->client_connected(client, capabilities);
  }
}

extern "C" void AgentClientConnectedTrampoline(const char* client,
                                               const char* capabilities,
                                               void* user) {
  auto* state = static_cast<AgentUiState*>(user);
  CefPostTask(TID_UI,
              base::BindOnce(&AgentUiClientConnected, base::Unretained(state),
                             std::string(client ? client : ""),
                             std::string(capabilities ? capabilities : "")));
}

}  // namespace

BrowserApp::BrowserApp(
    ::crayon::browser::localization::LocaleSnapshot locale_snapshot)
    : about_resources_(
          new branding::AboutBrowserResources(locale_snapshot.locale)),
      locale_snapshot_(locale_snapshot),
      product_strings_(BuildProductStringsOrEmpty(locale_snapshot)),
      page_markdown_strings_(
          BuildPageMarkdownStrings(product_strings_.page_markdown)),
      mdv_runtime_(std::make_shared<mdv::MdvRuntimeState>()),
      mdv_entries_(std::make_shared<mdv::MdvEntryController>(
          mdv_runtime_, product_strings_.mdv)),
      mdv_editing_(std::make_shared<mdv::MdvEditController>(
          mdv_runtime_, product_strings_.mdv)),
      permission_store_(std::make_unique<permission::PermissionStore>()),
      content_host_(std::make_unique<macos::ContentHostAdapter>()),
      media_host_(std::make_unique<media_host::MediaHostAdapter>(
          std::make_unique<macos::MediaHostProcess>())),
      cast_shell_(std::make_unique<media_host::CastShellController>(
          media_host::CastCommandPort{
              [this](media_host::media_host_ipc::DiscoveryAction action) {
                return media_host_->RequestDiscovery(action);
              },
              [this](std::optional<std::uint64_t> revision,
                     std::uint16_t offset) {
                return media_host_->RequestDevicePage(revision, offset);
              },
              [this](std::uint64_t candidate, std::string device,
                     bool handoff) {
                return media_host_->RequestStartCast(
                    candidate, std::move(device), handoff);
              },
              [this](std::uint64_t generation) {
                return media_host_->RequestStopCast(generation);
              },
              [this](std::string cast_code) {
                return media_host_->RequestResolveCastCode(
                    std::move(cast_code));
              },
              [this](std::uint64_t generation,
                     media_host::media_host_ipc::CastControlAction action,
                     std::optional<std::uint64_t> position) {
                return media_host_->RequestControlCast(generation, action,
                                                       position);
              }})),
      cast_chrome_strings_(BuildCastChromeStrings(product_strings_.cast)),
      trusted_input_monitor_(std::make_unique<macos::TrustedInputMonitor>()),
      tab_controller_(new window::TabController(
          kInitialUrl,
          [this](CefRefPtr<CefBrowser> browser) {
            // C20c: the titlebar cast button mounts per browser; Chromium
            // owns the tab display itself. Model-driven cast binding still
            // keys off the active tab.
            if (cast_chrome_ && browser) {
              const int browser_id = browser->GetIdentifier();
              static_cast<void>(cast_chrome_->AttachWindow(
                  browser_id, browser->GetHost()->GetWindowHandle()));
              cast_chrome_->SetActiveWindow(browser_id);
              RenderCastChrome();
            }
          },
          std::string(kInitialUrl), permission_store_.get())) {}

BrowserApp::~BrowserApp() {
  // Last-resort stop: the normal path (SetBrowsersClosedCallback) already
  // shut the host down on the UI thread, but destruction can also run on
  // a failed-init path where no CEF loop exists, so no thread assertion
  // here. Gate + FFI stop are thread-safe.
  if (agent_host_ && agent_host_->started()) {
    if (agent_ui_state_) {
      agent_ui_state_->Shutdown();
    }
    agent_host_->stop();
  }
}

// --- AGT-12Cc2r: agent-host assembly ------------------------------------

void BrowserApp::StartAgentHost() {
  CEF_REQUIRE_UI_THREAD();
  if (agent_host_ && agent_host_->started()) {
    return;
  }
  if (!agent_ui_state_) {
    agent_ui_state_ = std::make_unique<AgentUiState>();
  }
  agent_ui_state_->resolve_active_tab = [this]() {
    return AgentActiveTabIdForUi();
  };
  agent_ui_state_->tab_known = [this](const std::string& tab) {
    return AgentTabKnownForUi(tab);
  };
  agent_ui_state_->execute_tool =
      [this](const std::string& tool,
             const std::string& tab) -> std::optional<std::string> {
    return AgentExecuteToolForUi(tool, tab);
  };
  agent_ui_state_->client_connected =
      [this](const std::string& client, const std::string& capabilities) {
        OnAgentClientConnectedForUi(client, capabilities);
      };
  if (!agent_host_) {
    agent_host_ = std::make_unique<macos::AgentHostBridgeMac>();
  }
  const char* capabilities[] = {kAgentHostCapabilityPageRead};
  const macos::AgentHostBridgeMac::Callbacks callbacks{
      &AgentResolveActiveTabTrampoline, &AgentTabKnownTrampoline,
      &AgentExecuteTrampoline,          &AgentClientConnectedTrampoline,
      agent_ui_state_.get()};
  // A failed start means the agent feature is silently unavailable (no
  // endpoint, CLI connects fail); there is no success to announce and
  // diagnostics deliberately stay out of the hot path.
  static_cast<void>(agent_host_->start(kAgentHostPurpose, kAgentHostProfile,
                                       kAgentHostGrantTtlMs, capabilities, 1,
                                       callbacks));
  // No grant is minted here: grants are the AGT-05 confirmation outcome.
  // Without a confirmation surface every tool call fails with the stable
  // CapabilityDenied documented by AGT-16; start only exposes the UDS
  // endpoint and completes the handshake path.
}

void BrowserApp::ShutdownAgentHost() {
  CEF_REQUIRE_UI_THREAD();
  if (!agent_host_ || !agent_host_->started()) {
    return;
  }
  if (agent_ui_state_) {
    // Release bounded waiter waits before the FFI stop joins the thread.
    agent_ui_state_->Shutdown();
  }
  agent_host_->stop();
}

void BrowserApp::StopBackgroundServices() {
  CEF_REQUIRE_UI_THREAD();

  if (background_services_stopped_) {
    return;
  }
  background_services_stopped_ = true;
  // An open connect-confirmation sheet would keep the window's terminate
  // flow alive; end it (deny path) before tearing anything down.
  agent_confirm::DismissConnectConfirmPanel();
  content_host_tick_active_ = false;
  trusted_input_monitor_->Stop();
  // Created in OnContextInitialized, so an early quit (before any browser)
  // can still reach the funnel without it.
  if (page_markdown_preview_) {
    page_markdown_preview_->Stop();
  }
  // C20c: the cast shell owns in-flight cast commands; stop it before the
  // media host pipe goes away, then drop the titlebar surfaces.
  if (cast_shell_) cast_shell_->Shutdown();
  if (cast_chrome_) cast_chrome_->Close();
  ShutdownAgentHost();
  content_host_->Stop();
  media_host_->Stop();
}

// C20c: projects the shell controller's closed presentation onto the
// titlebar cast button/picker. No-op when nothing changed — the AppKit
// accessory rebuild is not free and the tick runs at 20 ms.
void BrowserApp::UpdateCastToolbarColor() {
  // The compact mask paints with the live toolbar background so the omnibox
  // pill visually ends before the cast button; theme switches re-resolve it.
  const auto browser = tab_controller_ ? tab_controller_->ActiveBrowser()
                                       : nullptr;
  const CefRefPtr<CefBrowserView> view =
      browser ? CefBrowserView::GetForBrowser(browser) : nullptr;
  const CefRefPtr<CefView> toolbar = view ? view->GetChromeToolbar() : nullptr;
  if (!toolbar) {
    return;
  }
  cast_chrome_->SetToolbarColor(
      static_cast<std::uint32_t>(toolbar->GetThemeColor(CEF_ColorToolbar)));
}

void BrowserApp::RenderCastChrome() {
  CEF_REQUIRE_UI_THREAD();
  if (!cast_chrome_ || !cast_shell_) {
    return;
  }
  UpdateCastToolbarColor();
  const auto presentation = cast_shell_->presentation();
  if (rendered_cast_presentation_ &&
      *rendered_cast_presentation_ == presentation) {
    return;
  }
  rendered_cast_presentation_ = presentation;
  cast_chrome_->Render(
      cast_shell_->coordinator(),
      macos::CastChromePresentation{presentation.cast_code_pending,
                                    presentation.cast_code_failed,
                                    presentation.control_pending,
                                    presentation.control_failed,
                                    presentation.playback_paused});
}

void BrowserApp::RequestProductQuit(bool force_close_browsers) {
  CEF_REQUIRE_UI_THREAD();

  // Quit may only reach the message loop from TabController's last
  // OnBeforeClose (browsers-closed callback already ran above) — quitting
  // earlier leaves live CEF objects and the agent-host serve thread behind,
  // which CHECK-fails inside CefShutdown.
  StopBackgroundServices();
  // The standalone settings window is outside the tab model; park the quit
  // until its OnBeforeClose releases it, then resume with the tab model.
  if (settings_browser_ && settings_browser_->IsValid()) {
    settings_quit_pending_ = true;
    settings_browser_->GetHost()->CloseBrowser(force_close_browsers);
    return;
  }
  tab_controller_->CloseAllBrowsers(force_close_browsers);
}

std::string BrowserApp::AgentActiveTabIdForUi() {
  CEF_REQUIRE_UI_THREAD();
  // The TabController owns tab activation; cast binding state
  // (active_browser_id_) is intentionally not consulted here so agent
  // reads stay independent of cast readiness.
  const auto browser = tab_controller_->ActiveBrowser();
  if (!browser) {
    return {};
  }
  const window::TabSnapshot* tab =
      tab_controller_->model().FindByBrowser(browser->GetIdentifier());
  if (!tab) {
    return {};
  }
  return std::to_string(tab->id);
}

bool BrowserApp::AgentTabKnownForUi(const std::string& tab) {
  CEF_REQUIRE_UI_THREAD();
  const std::optional<std::uint64_t> id = ParseTabIdText(tab);
  if (!id) {
    return false;
  }
  return tab_controller_->model().Find(static_cast<window::TabId>(*id)) !=
         nullptr;
}

std::string BrowserApp::AgentExecuteToolForUi(const std::string& tool,
                                              const std::string& tab) {
  CEF_REQUIRE_UI_THREAD();
  const window::TabModel& model = tab_controller_->model();
  if (tool == "page.list_targets") {
    std::string json = "{\"targets\":[";
    bool first = true;
    for (const window::TabId id : model.ordered_tabs()) {
      const window::TabSnapshot* snapshot = model.Find(id);
      if (!snapshot) {
        continue;
      }
      if (!first) {
        json.push_back(',');
      }
      first = false;
      json += "{\"id\":\"" + std::to_string(snapshot->id) + "\",\"active\":";
      json += model.active_tab() == std::optional<window::TabId>(snapshot->id)
                  ? "true"
                  : "false";
      json += ",\"loading\":";
      json += snapshot->loading ? "true" : "false";
      json += ",\"url\":\"" + JsonEscape(snapshot->url) + "\"}";
    }
    json += "]}";
    return json;
  }
  if (tool == "page.get_title") {
    const std::optional<std::uint64_t> id = ParseTabIdText(tab);
    const window::TabSnapshot* snapshot =
        id ? model.Find(static_cast<window::TabId>(*id)) : nullptr;
    if (!snapshot) {
      return {};
    }
    // The product tab strip surfaces the tab URL as its label today; the
    // preview serves the same value instead of inventing a page title.
    return "{\"tab_id\":\"" + std::to_string(snapshot->id) + "\",\"title\":\"" +
           JsonEscape(snapshot->url) + "\"}";
  }
  return {};
}

void BrowserApp::OnAgentClientConnectedForUi(const std::string& client,
                                             const std::string& capabilities) {
  CEF_REQUIRE_UI_THREAD();
  // Only the assembled read capability is grantable here; anything else
  // was already refused at advertisement time.
  if (capabilities.find(kAgentHostCapabilityPageRead) == std::string::npos) {
    return;
  }
  if (!agent_host_ || !agent_host_->started()) {
    return;
  }
  const localization::LocaleCatalog catalog(locale_snapshot_.locale);
  const std::string title =
      std::string(catalog.Find("agent.confirm.title").value_or(""));
  const std::string allow =
      std::string(catalog.Find("agent.confirm.allow").value_or(""));
  const std::string deny =
      std::string(catalog.Find("agent.confirm.deny").value_or(""));
  if (title.empty() || allow.empty() || deny.empty()) {
    return;
  }
  const std::string detail =
      std::string(catalog.Find("agent.confirm.client").value_or("Client")) +
      ": " + client + "\n" +
      std::string(catalog.Find("agent.confirm.disclosure").value_or(""));
  // Allow mints the session grant bound to THIS confirmed client; the
  // host refuses the mint once a different connection is active, so a
  // stale panel can never authorize a client the user never confirmed.
  agent_confirm::PresentConnectConfirmPanel(
      title, detail, allow, deny, {[this, client] {
        static_cast<void>(agent_host_->issue_grant_for_client(
            client.c_str(), kAgentHostCapabilityPageRead));
      }});
}

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  static_cast<void>(process_type);
  command_line->AppendSwitch("use-mock-keychain");
}

void BrowserApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  new_tab::RegisterCrayonCustomSchemes(registrar);
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  new_tab::RegisterNewTabSchemeHandlerFactory(
      browser_new_tab::BuildNewTabPageModel(
          browser_new_tab::NewTabProfileMode::kRegular, {}),
      product_strings_.new_tab);
  if (!mdv::RegisterMdvSchemeHandlerFactory(product_strings_.mdv,
                                            mdv_runtime_)) {
    CefQuitMessageLoop();
    return;
  }
  tab_controller_->SetLocalEntryCommandHandler(
      [entries = mdv_entries_, editing = mdv_editing_](
          CefRefPtr<CefBrowser> browser, int command_id) {
        if (entries->HandleChromeCommand(browser, command_id)) return true;
        return editing->HandleSaveCommand(browser, command_id);
      });
  mdv_entries_->SetDocumentLoadedCallback(
      [editing = mdv_editing_](CefRefPtr<CefBrowser> browser,
                               const std::string& path,
                               const std::string& normalized,
                               std::uint64_t size, std::uint64_t mtime) {
        editing->OnDocumentLoaded(browser, path, normalized, size, mtime);
      });
  tab_controller_->SetNavigationInterceptor([editing = mdv_editing_,
                                             entries = mdv_entries_](
                                                CefRefPtr<CefBrowser> browser,
                                                const CefString& url,
                                                bool user_gesture) {
    if (editing->InterceptWhileDirty(browser, url.ToString(), user_gesture)) {
      return true;
    }
    return entries->InterceptNavigation(browser, url, user_gesture);
  });
  tab_controller_->SetLocalEntryDragHandler(
      [entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefDragData> drag_data,
                               CefDragHandler::DragOperationsMask mask) {
        return entries->HandleDragEnter(browser, drag_data, mask);
      });
  tab_controller_->SetContextMenuAugmenter(
      [this, entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefContextMenuParams> params,
                                     CefRefPtr<CefMenuModel> model) {
        const bool mdv =
            entries->HandleContextMenuAugment(browser, params, model);
        const bool page_markdown =
            page_markdown_preview_->HandleContextMenuAugment(browser, params,
                                                             model);
        return mdv || page_markdown;
      });
  tab_controller_->SetContextMenuCommandHandler(
      [this, entries = mdv_entries_](CefRefPtr<CefBrowser> browser,
                                     int command_id) {
        if (entries->HandleContextMenuCommand(browser, command_id)) return true;
        return page_markdown_preview_->HandleContextMenuCommand(browser,
                                                                command_id);
      });
  tab_controller_->SetSaveCommandHandler(
      [editing = mdv_editing_](CefRefPtr<CefBrowser> browser) {
        return editing->SaveWriteBack(browser);
      });
  tab_controller_->SetPageQueryHandler(
      [editing = mdv_editing_](
          CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
          std::int64_t query_id, const CefString& request, bool persistent,
          CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
        return editing->OnPageQuery(browser, frame, query_id, request,
                                    persistent, std::move(callback));
      });
  tab_controller_->SetPageSnapshotObserver(content_host_.get());
  static_cast<void>(
      trusted_input_monitor_->Start([controller = tab_controller_] {
        controller->NoteTrustedUserInputForActiveTab();
      }));
  page_markdown_preview_ =
      std::make_unique<page_markdown::CefPageMarkdownPreviewController>(
          tab_controller_.get(), mdv_editing_, page_markdown_strings_,
          macos::CopyMarkdownToPasteboard);
  tab_controller_->SetPageSnapshotAdmission(
      [host = content_host_.get()] { return host->healthy(); });
  tab_controller_->SetPageSnapshotEventsReadyCallback([this] {
    content_host_->Consume(tab_controller_->DrainPageSnapshots(16));
  });
  tab_controller_->SetMediaObservationLifecycleCallback(
      [this](std::uint32_t tab_id, std::uint64_t navigation_id,
             std::uint32_t generation, bool closed) {
        const bool active = tab_controller_->model().active_tab() == tab_id;
        if (closed) {
          media_generations_.erase(tab_id);
          static_cast<void>(media_host_->CloseTab(tab_id, generation));
          if (active && cast_shell_) cast_shell_->OnPageClosed();
        } else {
          media_generations_[tab_id] = generation;
          if (tab_controller_->model().active_tab() == tab_id) {
            // C20b: the observation owner's navigation advance is already
            // driven per-navigation from OnLoadingUpdated; calling it here
            // re-entered the bridge (AdvanceNavigation → BindCurrentMainFrame
            // → this callback → …) and overflowed the stack.
            if (cast_shell_) cast_shell_->OnNavigation();
          } else {
            static_cast<void>(media_host_->AdvanceNavigation(
                tab_id, navigation_id, generation));
          }
        }
      });
  // C20a: browser close is Chromium's own in the Chrome-style window. The
  // shell registers no close-requested hook — WindowClient::DoClose returns
  // false and CEF tears the browser down with its window, then the quit
  // funnel runs from OnBeforeClose/OnWindowDestroyed.
  tab_controller_->SetBrowserFocusedCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        // C20c: a Chrome-style tab switch surfaces as the incoming tab's
        // focus; re-point the titlebar cast button at it.
        if (cast_chrome_ && browser) {
          const int browser_id = browser->GetIdentifier();
          if (active_browser_id_ != 0 && active_browser_id_ != browser_id &&
              cast_shell_) {
            cast_shell_->OnNavigation();
          }
          static_cast<void>(cast_chrome_->AttachWindow(
              browser_id, browser->GetHost()->GetWindowHandle()));
          cast_chrome_->SetActiveWindow(browser_id);
          RenderCastChrome();
        }
      });
  tab_controller_->SetBrowserClosingCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        if (cast_chrome_) {
          cast_chrome_->DetachWindow(browser->GetIdentifier());
        }
        // C20a: view release is Chromium's own in the Chrome-style window.
        if (active_browser_id_ == browser->GetIdentifier()) {
          active_browser_id_ = 0;
        }
      });
  tab_controller_->SetMediaObservationEventsReadyCallback(
      [this] { ConsumeMediaObservations(); });
  tab_controller_->SetBrowsersClosedCallback(
      [this] { StopBackgroundServices(); });
  if (!content_host_->Start(HelperExecutablePath("crayon-content-host")) ||
      !media_host_->Start(HelperExecutablePath("crayon-media-host"))) {
    content_host_->Stop();
    media_host_->Stop();
    CefQuitMessageLoop();
    return;
  }
  ContinueContentHostStartup();
}

void BrowserApp::ContinueContentHostStartup() {
  CEF_REQUIRE_UI_THREAD();
  if (content_host_->healthy() && media_host_->healthy()) {
    // C20a (roadmap §114): the product main window is CEF's own Chrome-style
    // window (native tab strip/toolbar/close/popups/chrome:// pages). The
    // shell registers no chrome surfaces of its own; the cast action button
    // mounts as a titlebar accessory in C20c (CastChromeMac).
    // AGT-12Cc2r: start the CAAP agent host (UDS endpoint) once both
    // helper hosts are healthy. Callbacks marshal onto this UI thread.
    StartAgentHost();
    // C20c: the titlebar cast button + picker. Actions run through the
    // shell controller; every completion re-renders from its closed state.
    if (!cast_chrome_ && CastChromeStringsComplete(cast_chrome_strings_)) {
      cast_chrome_ = std::make_unique<macos::CastChromeMac>(
          cast_chrome_strings_,
          macos::CastChromeCallbacks{
              [this] {
                // Plan A (roadmap §114 C20f): the cast-button press is the
                // strongest user-intent signal. Bank it first so autoplay
                // that was already running stops denying eligibility; real
                // playback progress after the press still gates the cast.
                tab_controller_->NoteCastIntentForActiveTab();
                const bool ok =
                    cast_shell_ && cast_shell_->ActivateCastButton();
                RenderCastChrome();
                return ok;
              },
              [this] {
                const bool ok =
                    cast_shell_ && cast_shell_->RefreshReceivers();
                RenderCastChrome();
                return ok;
              },
              [this] {
                if (cast_shell_) cast_shell_->CancelReceiverPicker();
                RenderCastChrome();
              },
              [this](const std::string& device_id) {
                const bool ok =
                    cast_shell_ && cast_shell_->SelectReceiver(device_id);
                RenderCastChrome();
                return ok;
              },
              [this](std::string cast_code) {
                const bool ok =
                    cast_shell_ &&
                    cast_shell_->ConnectCastCode(std::move(cast_code));
                RenderCastChrome();
                return ok;
              },
              [this](bool paused) {
                const bool ok = cast_shell_ && cast_shell_->SetPaused(paused);
                RenderCastChrome();
                return ok;
              },
              [this](std::uint64_t seconds) {
                const bool ok =
                    cast_shell_ && cast_shell_->SeekSession(seconds);
                RenderCastChrome();
                return ok;
              }});
    }
    // The initial page is the built-in new tab; new tabs created through
    // Chromium's own UI are adopted by TabController.
    if (!tab_controller_->CreateMainWindow()) {
      // The window never existed, so this is a pure service-teardown exit;
      // the funnel stops the chain and quits with no browsers left.
      RequestProductQuit(true);
      return;
    }
    content_host_tick_active_ = true;
    ScheduleContentHostTick();
    return;
  }
  if (++content_host_start_checks_ >= kContentHostStartupChecks) {
    content_host_->Stop();
    media_host_->Stop();
    CefQuitMessageLoop();
    return;
  }
  CefPostDelayedTask(TID_UI,
                     CefCreateClosureTask(
                         base::BindOnce(&BrowserApp::ContinueContentHostStartup,
                                        CefRefPtr<BrowserApp>(this))),
                     kContentHostTickMilliseconds);
}

void BrowserApp::ScheduleContentHostTick() {
  CefPostDelayedTask(
      TID_UI,
      CefCreateClosureTask(base::BindOnce(&BrowserApp::ContentHostTick,
                                          CefRefPtr<BrowserApp>(this))),
      kContentHostTickMilliseconds);
}

void BrowserApp::ContentHostTick() {
  CEF_REQUIRE_UI_THREAD();
  if (!content_host_tick_active_) return;
  content_host_->Consume(tab_controller_->DrainPageSnapshots(16));
  ConsumeMediaObservations();
  content_host_->Tick();
  media_host_->Tick();
  page_markdown_preview_->Tick(content_host_->Drain(64),
                               content_host_->healthy());
  static_cast<void>(media_host_->Drain(64));
  // C20c: cast command replies and planning events feed the shell
  // controller; the titlebar button/picker re-renders on state changes.
  if (cast_shell_) {
    cast_shell_->ConsumeCast(media_host_->DrainCast(64));
    cast_shell_->ConsumePlanning(media_host_->DrainPlanning(64));
    RenderCastChrome();
  }
  ScheduleContentHostTick();
}

void BrowserApp::ConsumeMediaObservations() {
  CEF_REQUIRE_UI_THREAD();
  std::vector<media_host::BrowserMediaFact> facts;
  for (auto& event : tab_controller_->DrainMediaObservations(16)) {
    auto page_url =
        tab_controller_->TrustedPageUrl(event.tab_id, event.navigation_id);
    if (!page_url) continue;
    facts.push_back(media_host::BrowserMediaFact{
        std::move(event), std::move(*page_url), MonotonicMilliseconds()});
  }
  media_host_->Consume(std::move(facts));
  // C20c/f: a verified-media event is the browser-verified playback proof
  // the cast shell needs before its button may light up.
  for (const auto &fact : facts) {
    if (fact.observation.source == ::crayon::cef_shell::gateway::EventSource::kMedia) {
      cast_shell_->OnBrowserVerifiedMedia();
      break;
    }
  }
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

// PLT-SHELL-24M2FIX-C11: chrome:// WebUI renders only in Chrome runtime
// style; an Alloy browser refuses the navigation silently (measured on the
// product 2026-09-23: the command ran, LoadURL returned, the page stayed
// unchanged). The settings window is therefore a standalone Chrome-style
// browser with its own minimal client — deliberately outside the
// TabController tab model (no tab strip entry, no cast/snapshot/media
// observation, no agent surface): chrome://settings is read-only UI.
class SettingsWindowClient final : public CefClient,
                                   public CefLifeSpanHandler {
 public:
  explicit SettingsWindowClient(BrowserApp* app) : app_(app) {}

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    if (app_ && browser) {
      app_->settings_browser_ = browser;
    }
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    static_cast<void>(browser);
    if (app_) {
      app_->OnSettingsBrowserClosed();
    }
    app_ = nullptr;
  }

 private:
  BrowserApp* app_;

  IMPLEMENT_REFCOUNTING(SettingsWindowClient);
  DISALLOW_COPY_AND_ASSIGN(SettingsWindowClient);
};

// PLT-SHELL-24M2FIX-C11: the product owns no settings surface of its own.
// Chromium's own settings page is the single settings destination, so every
// entry point opens it in the standalone Chrome-style window. Loading it
// from a command (rather than accepting the chrome scheme in the omnibox)
// keeps IsAllowedNavigationUrl unchanged.
// C20a: chrome:// pages render natively in the Chrome-style window, so
// settings load into the active tab. The standalone Chrome-style window
// (SettingsWindowClient) remains the fallback when no tab exists.
void BrowserApp::OpenOriginalSettings() {
  CEF_REQUIRE_UI_THREAD();
  const auto browser =
      tab_controller_ ? tab_controller_->ActiveBrowser() : nullptr;
  if (browser && browser->GetMainFrame()) {
    browser->GetMainFrame()->LoadURL(kOriginalSettingsUrl);
    return;
  }
  if (settings_browser_ && settings_browser_->IsValid()) {
    // One settings window: a repeated command must not stack windows.
    return;
  }
  settings_client_ = new SettingsWindowClient(this);
  CefWindowInfo window_info;
  window_info.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  CefBrowserSettings browser_settings;
  if (!CefBrowserHost::CreateBrowser(window_info, settings_client_,
                                     kOriginalSettingsUrl, browser_settings,
                                     nullptr, nullptr)) {
    settings_client_ = nullptr;
  }
}

void BrowserApp::OnSettingsBrowserClosed() {
  CEF_REQUIRE_UI_THREAD();
  settings_browser_ = nullptr;
  settings_client_ = nullptr;
  // A quit that arrived while the window was open resumes here: the message
  // loop may only quit once no browser is left (CefShutdown CHECKs it).
  if (settings_quit_pending_) {
    settings_quit_pending_ = false;
    if (tab_controller_) {
      tab_controller_->CloseAllBrowsers(true);
    }
  }
}


bool BrowserApp::ExecuteAppCommand(macos::ApplicationCommand command) {
  CEF_REQUIRE_UI_THREAD();
  switch (command) {
    case macos::ApplicationCommand::kNewTab: {
      // C20a: tabs are Chromium's own in the Chrome-style window; the app
      // menu's new-tab entry rides the native IDC_NEW_TAB command.
      const auto browser = tab_controller_->ActiveBrowser();
      static const int kNewTabCommandId =
          cef_id_for_command_id_name("IDC_NEW_TAB");
      if (!browser || kNewTabCommandId <= 0) {
        return false;
      }
      browser->GetHost()->ExecuteChromeCommand(kNewTabCommandId,
                                               CEF_WOD_NEW_FOREGROUND_TAB);
      return true;
    }
    case macos::ApplicationCommand::kCloseTab:
      if (!tab_controller_->ActiveBrowser()) {
        return false;
      }
      tab_controller_->CloseActiveTab();
      return true;
    case macos::ApplicationCommand::kFocusLocation:
      // C20b: the Alloy omnibox is gone; the Chrome-style window keeps its
      // own location-bar focus (IDC_FOCUS_LOCATION rides the native path).
      return false;
    case macos::ApplicationCommand::kSettings:
      // PLT-SHELL-24M2FIX-C11: the toolbar menu's settings entry (and the
      // application menu's, which routes through this same handler) opens
      // the Chromium own settings page.
      OpenOriginalSettings();
      return true;
    case macos::ApplicationCommand::kReload:
      tab_controller_->Reload();
      return true;
    case macos::ApplicationCommand::kBack:
      tab_controller_->GoBack();
      return true;
    case macos::ApplicationCommand::kForward:
      tab_controller_->GoForward();
      return true;
    default:
      return false;
  }
}

bool BrowserApp::product_strings_valid() const {
  return ::crayon::browser::product_strings::ProductStringsAreComplete(
      product_strings_);
}

}  // namespace crayon::browser::cef_shell
