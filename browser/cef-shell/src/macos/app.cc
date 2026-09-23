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
#include "crayon/browser_preferences/preference_codec.h"
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
      trusted_input_monitor_(std::make_unique<macos::TrustedInputMonitor>()),
      tab_controller_(new window::TabController(
          kInitialUrl,
          [this](CefRefPtr<CefBrowser> browser) {
            const int browser_id = browser->GetIdentifier();
            const window::TabSnapshot* tab =
                tab_controller_->model().FindByBrowser(browser_id);
            // TabModel::CreateTab auto-activated the new tab: surface it.
            if (tab && product_host_) {
              product_host_->ShowBrowser(browser_id);
            }
            if (tab && toolbar_) {
              static_cast<void>(toolbar_->AttachBrowser(tab->id, browser));
              static_cast<void>(toolbar_->SyncTabs(tab_controller_->model()));
              if (product_host_) product_host_->RefreshTabChrome();
            }
            BindCastForActiveTab();
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
  ResetCastContext();
  if (toolbar_) toolbar_->Shutdown();
  ShutdownAgentHost();
  content_host_->Stop();
  media_host_->Stop();
}

void BrowserApp::RequestProductQuit(bool force_close_browsers) {
  CEF_REQUIRE_UI_THREAD();
  // Quit may only reach the message loop from TabController's last
  // OnBeforeClose (browsers-closed callback already ran above) — quitting
  // earlier leaves live CEF objects and the agent-host serve thread behind,
  // which CHECK-fails inside CefShutdown.
  StopBackgroundServices();
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
        if (closed) {
          media_generations_.erase(tab_id);
          if (cast_binding_attempt_ &&
              cast_binding_attempt_->tab_id == tab_id) {
            ResetCastContext();
          } else {
            static_cast<void>(media_host_->CloseTab(tab_id, generation));
          }
        } else {
          media_generations_[tab_id] = generation;
          if (tab_controller_->model().active_tab() == tab_id) {
            BindCastForActiveTab();
          } else {
            static_cast<void>(media_host_->AdvanceNavigation(
                tab_id, navigation_id, generation));
          }
        }
      });
  tab_controller_->SetBrowserCloseRequestedCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        if (!product_host_ || !browser ||
            !product_host_->browser_view(browser->GetIdentifier()))
          return false;
        if (active_browser_id_ == browser->GetIdentifier())
          DetachCastSurface();
        return product_host_->HandleBrowserClose(browser);
      });
  tab_controller_->SetBrowserFocusedCallback(
      [this](CefRefPtr<CefBrowser>) { SyncToolbarToActiveTab(); });
  tab_controller_->SetBrowserClosingCallback(
      [this](CefRefPtr<CefBrowser> browser) {
        if (product_host_)
          product_host_->NotifyBrowserClosed(browser->GetIdentifier());
        if (active_browser_id_ == browser->GetIdentifier()) {
          DetachCastSurface();
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
    // PLT-SHELL-24M2: the product toolbar assembly (tab strip + navigation +
    // omnibox) shares the Alloy window with the TabController WindowClient.
    if (!toolbar_) {
      toolbar_ = std::make_unique<macos::AlloyToolbarMac>(
          locale_snapshot_,
          macos::AlloyToolbarMac::Callbacks{
              [this] {
                if (product_host_) {
                  // PLT-SHELL-24M2FIX-C9: "+" opens the user's new-tab URL
                  // (product default www.zknowai.com), not the built-in page.
                  static_cast<void>(product_host_->CreateTab(NewTabUrl()));
                }
              },
              [this](window::TabId tab_id) {
                if (tab_controller_->ActivateTab(tab_id)) {
                  SyncToolbarToActiveTab();
                }
              },
              [this](window::TabId tab_id) {
                static_cast<void>(tab_controller_->RequestCloseTab(tab_id));
              },
              [this](window::TabId tab_id) {
                const window::TabSnapshot* tab =
                    tab_controller_->model().Find(tab_id);
                if (!tab) {
                  return std::string{};
                }
                // PLT-SHELL-24M2UIP-b: prefer the page title; the URL stays
                // as the fallback until the first title change arrives.
                return !tab->title.empty() ? tab->title : tab->url;
              },
              // PLT-SHELL-24M2FIX-C6: the address bar's bookmark control only
              // reports the press; the store and the resulting state live here.
              [this] { ToggleActiveBookmark(); },
              // PLT-SHELL-24M2FIX-C7: the toolbar menu sends the same command
              // ids the application menu does, so both entry points share one
              // handler instead of growing a second command implementation.
              [this](int command_id) {
                static_cast<void>(
                    ExecuteAppCommand(static_cast<macos::ApplicationCommand>(
                        command_id)));
              }});
      tab_controller_->SetTabUiUpdateCallback(
          [this](int browser_id, const std::string& url, bool is_loading,
                 bool can_go_back, bool can_go_forward) {
            if (!toolbar_) {
              return;
            }
            static_cast<void>(toolbar_->OnTabUiUpdate(
                browser_id, url, is_loading, can_go_back, can_go_forward));
            if (browser_id == 0) {
              SyncToolbarToActiveTab();
            } else {
              static_cast<void>(toolbar_->SyncTabs(tab_controller_->model()));
              if (product_host_) product_host_->RefreshTabChrome();
            }
            // PLT-SHELL-24M2FIX-D: capture the view tree at every navigation
            // state change, which is when "which tab is actually on screen"
            // stops matching "which tab just loaded".
            if (product_host_) {
              product_host_->DumpDiagnostics("tab-ui-update");
            }
          });
      // PLT-SHELL-24M2FIX-B: the macOS shell has no built-in-content observer
      // for load errors, so register the platform-neutral TabController
      // projection; otherwise a failed navigation leaves the toolbar in its
      // loading presentation with a blank page and no explanation.
      tab_controller_->SetTabLoadErrorCallback(
          [this](int browser_id, std::string url, bool certificate_error) {
            if (!toolbar_) {
              return;
            }
            static_cast<void>(toolbar_->OnTabLoadError(
                browser_id, url, certificate_error));
          });
    }
    // AGT-12Cc2r: start the CAAP agent host (UDS endpoint) once both
    // helper hosts are healthy. Callbacks marshal onto this UI thread.
    StartAgentHost();
    // PLT-SHELL-24M1: the product first window is the macOS Alloy host; the
    // TabController WindowClient keeps every normalized callback surface.
    if (!product_host_) {
      product_host_ = std::make_unique<macos::AlloyProductHostMac>(
          macos::AlloyProductHostMac::Dependencies{
              tab_controller_->client(), kInitialUrl,
              product_strings_.new_tab.document_title,
              toolbar_->tab_strip_view(), toolbar_->toolbar_view(),
              // PLT-SHELL-24M2FIX-C4: the assembly owns the chrome views, so
              // it is the only place that can answer where the tab corners,
              // tab loading indicator and omnibox pill are. The host decides
              // when to ask (window creation, every layout pass).
              [this] {
                return toolbar_ ? toolbar_->decoration()
                                : window::ChromeDecoration{};
              }},
          macos::AlloyProductHostMac::Callbacks{
              // window_destroyed: the window is gone, but any browser that
              // has not delivered OnBeforeClose yet must still be closed
              // through the quit funnel — a direct CefQuitMessageLoop here
              // exits the loop with live CEF objects behind.
              [this] { RequestProductQuit(true); },
              [this] { SyncToolbarToActiveTab(); },
              [this] { DetachCastSurface(); },
              [this] {
                if (cast_surface_) cast_surface_->LayoutChanged();
              },
              [this](const CefKeyEvent& event) {
                return cast_surface_ && cast_surface_->HandleKeyEvent(event);
              },
              [this](int command_id) {
                return cast_surface_ &&
                       cast_surface_->HandleAccelerator(command_id);
              }});
    }
    if (!product_host_->Start()) {
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
  if (cast_controller_ && cast_context_bound_)
    cast_controller_->Tick();
  else
    media_host_->Tick();
  page_markdown_preview_->Tick(content_host_->Drain(64),
                               content_host_->healthy());
  static_cast<void>(media_host_->Drain(64));
  static_cast<void>(media_host_->DrainPlanning(64));
  const bool media_healthy = media_host_->healthy();
  const std::uint64_t cast_epoch = media_host_->cast_state_epoch();
  if ((!media_healthy && media_host_was_healthy_) ||
      (cast_controller_ && cast_epoch != media_host_cast_epoch_)) {
    ResetCastContext();
  }
  media_host_was_healthy_ = media_healthy;
  media_host_cast_epoch_ = cast_epoch;
  // PLT-SHELL-24M2UIP-a: the entry re-attaches (grey) whenever teardown
  // paths cleared it, independent of media readiness.
  TryAttachCastEntry();
  if (media_healthy) BindCastForActiveTab();
  if (cast_surface_) cast_surface_->Tick();
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
}

CefRefPtr<CefClient> BrowserApp::GetDefaultClient() {
  return tab_controller_->client();
}

void BrowserApp::SyncToolbarToActiveTab() {
  CEF_REQUIRE_UI_THREAD();
  if (!toolbar_) {
    return;
  }
  if (CefRefPtr<CefBrowser> browser = tab_controller_->ActiveBrowser()) {
    const window::TabSnapshot* tab =
        tab_controller_->model().FindByBrowser(browser->GetIdentifier());
    if (tab) {
      // Browsing remains usable even while the media host is unavailable.
      if (product_host_) product_host_->ShowBrowser(browser->GetIdentifier());
      static_cast<void>(toolbar_->AttachBrowser(tab->id, browser));
    }
  }
  static_cast<void>(toolbar_->SyncTabs(tab_controller_->model()));
  // PLT-SHELL-24M2FIX-C6: the star follows the tab, not the click, so a tab
  // switch shows the new page's state without waiting for a toggle.
  static_cast<void>(RefreshBookmarkState());
  // PLT-SHELL-24M2FIX-C7: idempotent, and re-appends the trailing menu button
  // after whatever the cast surface mounts, so it always stays last.
  static_cast<void>(toolbar_->EnsureTrailingMenuButton());
  if (product_host_) product_host_->RefreshTabChrome();
  BindCastForActiveTab();
  // PLT-SHELL-24M2FIX-D: the product, not a look-alike probe, reports the
  // view tree at the moment the toolbar is bound to the active tab.
  if (product_host_) {
    product_host_->DumpDiagnostics("sync-toolbar");
  }
}

// PLT-SHELL-24M2FIX-C6: the bookmark store is created on first use and loaded
// from the profile's own data directory. A missing file is the first run, not a
// failure, so the load result is deliberately ignored; a failed create does
// leave the control inert rather than pretending the page was saved.
bool BrowserApp::EnsureBookmarks() {
  CEF_REQUIRE_UI_THREAD();
  if (bookmarks_) {
    return true;
  }
  const auto profile = browser_engine::ProfileId::TryCreate("crayon-default");
  if (!profile) {
    return false;
  }
  bookmarks_ = std::make_unique<window::AlloyBookmarks>(
      *profile,
      window::AlloyBookmarks::Callbacks{
          [this](const std::string& url) {
            return toolbar_ && toolbar_->NavigateToAddress(url);
          },
          [this](const std::string& url) {
            return product_host_ && product_host_->CreateTab(url);
          }});
  const auto context = CefRequestContext::GetGlobalContext();
  const std::string cache =
      context ? context->GetCachePath().ToString() : std::string{};
  if (!cache.empty()) {
    static_cast<void>(
        bookmarks_->LoadFromFile(cache + "/crayon-bookmarks.json"));
  }
  return true;
}

bool BrowserApp::SaveBookmarks() {
  CEF_REQUIRE_UI_THREAD();
  if (!bookmarks_) {
    return false;
  }
  const auto context = CefRequestContext::GetGlobalContext();
  const std::string cache =
      context ? context->GetCachePath().ToString() : std::string{};
  if (cache.empty()) {
    return false;
  }
  return bookmarks_->SaveToFile(cache + "/crayon-bookmarks.json");
}

// Reflects the active page's bookmark state on the address bar control.
bool BrowserApp::RefreshBookmarkState() {
  CEF_REQUIRE_UI_THREAD();
  if (!toolbar_ || !tab_controller_ || !EnsureBookmarks()) {
    return false;
  }
  const CefRefPtr<CefBrowser> browser = tab_controller_->ActiveBrowser();
  if (!browser) {
    return false;
  }
  const window::TabSnapshot* tab =
      tab_controller_->model().FindByBrowser(browser->GetIdentifier());
  // An empty or non-http(s) page cannot be bookmarked, so the control reports
  // "not saved" instead of keeping a stale filled state.
  if (!tab || tab->url.empty() ||
      !browser_bookmarks::BookmarkStore::IsValidUrl(tab->url)) {
    return toolbar_->SetBookmarked(false);
  }
  if (!bookmarks_->RefreshForUrl(tab->url)) {
    return false;
  }
  return toolbar_->SetBookmarked(bookmarks_->bar().current_page_starred());
}

void BrowserApp::ToggleActiveBookmark() {
  CEF_REQUIRE_UI_THREAD();
  if (!toolbar_ || !tab_controller_ || !EnsureBookmarks()) {
    return;
  }
  const CefRefPtr<CefBrowser> browser = tab_controller_->ActiveBrowser();
  if (!browser) {
    return;
  }
  const window::TabSnapshot* tab =
      tab_controller_->model().FindByBrowser(browser->GetIdentifier());
  if (!tab || tab->url.empty() ||
      !browser_bookmarks::BookmarkStore::IsValidUrl(tab->url)) {
    return;
  }
  const std::string url = tab->url;
  const std::string title = tab->title.empty() ? tab->url : tab->title;
  if (!bookmarks_->RefreshForUrl(url)) {
    return;
  }
  if (const auto existing = bookmarks_->bar().current_page_bookmark()) {
    static_cast<void>(bookmarks_->Remove(*existing));
  } else {
    static_cast<void>(bookmarks_->AddCurrentPage(title, url));
  }
  // Re-read the store rather than assuming the write: Remove() can refuse, and
  // the control must then keep showing the state the store actually holds.
  static_cast<void>(RefreshBookmarkState());
  static_cast<void>(SaveBookmarks());
}

// PLT-SHELL-24M2FIX-C9: preferences are created on first use and loaded from
// the profile's own data directory through the shared codec, so the on-disk
// shape is the same one the Windows shell writes.
bool BrowserApp::EnsurePreferences() {
  CEF_REQUIRE_UI_THREAD();
  if (preferences_) {
    return true;
  }
  preferences_ = std::make_unique<browser_preferences::PreferenceStore>();
  const auto context = CefRequestContext::GetGlobalContext();
  const std::string cache =
      context ? context->GetCachePath().ToString() : std::string{};
  if (cache.empty()) {
    return true;
  }
  std::ifstream input(cache + "/crayon-preferences.json", std::ios::binary);
  if (!input) {
    return true;  // First run: all keys at their defaults.
  }
  const std::string document((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  if (auto restored = browser_preferences::DeserializePreferences(document)) {
    preferences_ = std::make_unique<browser_preferences::PreferenceStore>(
        std::move(*restored));
  }
  return true;
}

bool BrowserApp::SavePreferences() {
  CEF_REQUIRE_UI_THREAD();
  if (!preferences_) {
    return false;
  }
  const auto context = CefRequestContext::GetGlobalContext();
  const std::string cache =
      context ? context->GetCachePath().ToString() : std::string{};
  if (cache.empty()) {
    return false;
  }
  std::ofstream output(cache + "/crayon-preferences.json",
                       std::ios::trunc | std::ios::binary);
  if (!output) {
    return false;
  }
  output << browser_preferences::SerializePreferences(*preferences_);
  return output.good();
}

// The URL the new-tab ("+") action opens. An empty stored value falls back to
// the built-in page; anything else is resolved like omnibox input, so a value
// typed as "www.zknowai.com" becomes https://... under the privacy defaults.
std::string BrowserApp::NewTabUrl() {
  CEF_REQUIRE_UI_THREAD();
  if (!EnsurePreferences()) {
    return kInitialUrl;
  }
  const auto &value =
      preferences_->Get(browser_preferences::PreferenceStore::kNewTabUrl);
  const auto *text = std::get_if<std::string>(&value);
  if (!text || text->empty()) {
    return kInitialUrl;
  }
  if (text->find("://") != std::string::npos) {
    return *text;
  }
  return browser_omnibox_provider::ResolveSchemelessUrl(
      *text, browser_privacy::DefaultPrivacyDefaults());
}

// Writes the new-tab URL after the store's own validation. A rejected value
// (over-long, control characters) leaves the previous one in place instead of
// persisting something the store would refuse to load.
bool BrowserApp::SetNewTabUrl(std::string value) {
  CEF_REQUIRE_UI_THREAD();
  if (!EnsurePreferences()) {
    return false;
  }
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  const browser_preferences::PreferenceValue candidate{value};
  if (!browser_preferences::PreferenceStore::IsValidValueForKey(
          browser_preferences::PreferenceStore::kNewTabUrl, candidate)) {
    return false;
  }
  if (!preferences_->Set(browser_preferences::PreferenceStore::kNewTabUrl,
                         candidate)) {
    return false;
  }
  return SavePreferences();
}

void BrowserApp::ShowSettings() {
  CEF_REQUIRE_UI_THREAD();
  if (!EnsurePreferences()) {
    return;
  }
  const auto &value =
      preferences_->Get(browser_preferences::PreferenceStore::kNewTabUrl);
  const auto *text = std::get_if<std::string>(&value);
  const std::string current = text ? *text : std::string{};
  const auto localized = [this](const char *key) {
    const auto found =
        ::crayon::browser::localization::LocaleCatalog(locale_snapshot_.locale)
            .Find(key);
    return found ? std::string(*found) : std::string{};
  };
  static_cast<void>(settings_panel::PresentSettingsPanel(
      localized("settings.title"), localized("settings.new_tab_url"), current,
      localized("settings.save"), localized("omnibox.cancel"),
      settings_panel::PanelCallbacks{[this](std::string entered) {
        // The panel is already gone; the value is validated and persisted here.
        static_cast<void>(SetNewTabUrl(std::move(entered)));
      }}));
}

void BrowserApp::DetachCastSurface() {
  if (cast_surface_) cast_surface_->Detach();
  cast_surface_.reset();
}

void BrowserApp::ResetCastContext() {
  DetachCastSurface();
  if (cast_controller_) cast_controller_->Shutdown();
  cast_controller_.reset();
  cast_binding_attempt_.reset();
  cast_context_bound_ = false;
  active_browser_id_ = 0;
}

void BrowserApp::BindCastForActiveTab() {
  CEF_REQUIRE_UI_THREAD();
  if (!product_host_ || !toolbar_ || !media_host_->healthy()) return;
  const auto browser = tab_controller_->ActiveBrowser();
  if (!browser) return;
  const auto* tab =
      tab_controller_->model().FindByBrowser(browser->GetIdentifier());
  if (!tab || !tab->navigation_generation) return;
  const auto generation =
      media_generations_.find(static_cast<std::uint32_t>(tab->id));
  const auto view = product_host_->browser_view(browser->GetIdentifier());
  const auto window = product_host_->window();
  if (generation == media_generations_.end() || !view || !window) return;
  if (active_browser_id_ != browser->GetIdentifier()) {
    // CloseTab retires the previous observation generation. Renew via the
    // existing Browser observation owner on activation, never synthesize it.
    active_browser_id_ = browser->GetIdentifier();
    tab_controller_->client()->AdvanceMediaObservationNavigation(
        browser, static_cast<std::uint32_t>(tab->id),
        tab->navigation_generation);
    return;  // The synchronous lifecycle callback performs the bind once.
  }
  if (!cast_controller_) {
    cast_browser_session_ = MonotonicMilliseconds();
    media_host_cast_epoch_ = media_host_->cast_state_epoch();
    media_host_was_healthy_ = true;
    const localization::LocaleCatalog catalog(locale_snapshot_.locale);
    cast_controller_ = std::make_unique<media_host::AlloyCastController>(
        media_host_.get(),
        [this](auto snapshot) {
          if (cast_surface_ && media_host_->healthy() &&
              media_host_->cast_state_epoch() == media_host_cast_epoch_)
            static_cast<void>(cast_surface_->Apply(std::move(snapshot)));
        },
        std::string(catalog.Find("cast.selection.video_fallback").value_or("")),
        std::string(
            catalog.Find("cast.selection.device_fallback").value_or("")),
        MonotonicMilliseconds);
  }
  const browser_cast_view::CastViewContext context{
      cast_browser_session_, "default", static_cast<std::uint32_t>(tab->id),
      tab->navigation_generation, generation->second};
  // One attempt per identity: readiness is checked above; a rejected bind must
  // not repeatedly close/advance the runtime tab on each 20ms tick.
  if (cast_binding_attempt_ && *cast_binding_attempt_ == context) return;
  DetachCastSurface();
  cast_binding_attempt_ = context;
  active_browser_id_ = browser->GetIdentifier();
  // PLT-SHELL-24M2UIP-a: the entry surface is attached at assembly time and
  // persists here; only the context binds once media data is available.
  TryAttachCastEntry();
  if (!cast_surface_) return;
  cast_surface_->BindContext(context);
  cast_context_bound_ = cast_controller_->BindContext(context);
  if (!cast_context_bound_) DetachCastSurface();
}

void BrowserApp::TryAttachCastEntry() {
  CEF_REQUIRE_UI_THREAD();
  if (cast_surface_ || !product_host_ || !toolbar_ || !media_host_ ||
      !tab_controller_) {
    return;
  }
  const auto browser = tab_controller_->ActiveBrowser();
  if (!browser) {
    return;
  }
  const auto window = product_host_->window();
  const auto view = product_host_->browser_view(browser->GetIdentifier());
  if (!window || !view) {
    return;
  }
  // Permanent toolbar fixture: attaches with the first browser view — long
  // before any media observation — and stays disabled (grey) via the
  // presentation (no context => EntryEnabled() == false) until a real MHV2
  // context binds in BindCastForActiveTab.
  cast_surface_ = std::make_unique<CastEntrySurface>(
      locale_snapshot_, MonotonicMilliseconds, [this](auto intent) {
        if (cast_controller_ && media_host_->healthy() &&
            media_host_->cast_state_epoch() == media_host_cast_epoch_)
          static_cast<void>(cast_controller_->HandleIntent(intent));
      });
  if (!cast_surface_->Attach(window, view, toolbar_->toolbar_panel())) {
    DetachCastSurface();
    return;
  }
  // PLT-SHELL-24M2FIX-C7: the cast entry was just appended to the toolbar row,
  // so re-append the trailing menu button to keep it last (the user-facing
  // order is: address bar, cast entry, menu).
  static_cast<void>(toolbar_->EnsureTrailingMenuButton());
}

bool BrowserApp::ExecuteAppCommand(macos::ApplicationCommand command) {
  CEF_REQUIRE_UI_THREAD();
  switch (command) {
    case macos::ApplicationCommand::kNewTab:
      return product_host_ && product_host_->CreateTab(NewTabUrl());
    case macos::ApplicationCommand::kCloseTab:
      if (!tab_controller_->ActiveBrowser()) {
        return false;
      }
      tab_controller_->CloseActiveTab();
      return true;
    case macos::ApplicationCommand::kFocusLocation:
      return toolbar_ && toolbar_->FocusOmnibox();
    case macos::ApplicationCommand::kSettings:
      // PLT-SHELL-24M2FIX-C9: the toolbar menu's settings entry (and the
      // application menu's, which routes through this same handler).
      ShowSettings();
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
