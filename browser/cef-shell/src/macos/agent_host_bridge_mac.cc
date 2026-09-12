// AGT-12Cc2: C++ adapter for the Rust agent-host staticlib. Every Rust
// FFI entry point is panic-free; this file maps the C ABI onto a small
// RAII bridge the product owns. The tool callbacks pass through as
// function pointers — the product (BrowserApp) provides the actual
// implementations and marshals any CEF work onto the UI thread.

#include "macos/agent_host_bridge_mac.h"

#include <string>
#include <vector>

extern "C" {
// crayon-agent-host staticlib (crates/crayon-agent-host/src/ffi.rs).
struct CrayonAgentHostExecuteResult {
  int status;
  char* text;
};

struct CrayonAgentHostConfig {
  const char* purpose;
  const char* profile;
  std::uint64_t grant_ttl_ms;
  const char* const* capabilities;
  void* resolve_active_tab_user;
  const char* (*resolve_active_tab_fn)(void* user);
  int (*tab_known_fn)(const char* tab, void* user);
  CrayonAgentHostExecuteResult (*execute_fn)(const char* tool,
                                             const char* request_json,
                                             const char* tab,
                                             int (*is_cancelled)(void* user),
                                             void* cancel_user,
                                             void* user);
  void* user_data;
};

int crayon_agent_host_start(const CrayonAgentHostConfig* config);
int crayon_agent_host_stop();
int crayon_agent_host_issue_grant(const char* capability);
char* crayon_agent_host_string_alloc(const char* src);
void crayon_agent_host_string_free(char* ptr);

// Status codes mirrored from ffi.rs.
constexpr int kAgentHostOk = 0;
}  // extern "C"

namespace crayon::browser::cef_shell::macos {

AgentHostBridgeMac::AgentHostBridgeMac() = default;

AgentHostBridgeMac::~AgentHostBridgeMac() {
  stop();
}

bool AgentHostBridgeMac::start(const char* purpose,
                               const char* profile,
                               std::uint64_t grant_ttl_ms,
                               const char* const* capabilities,
                               std::size_t capability_count,
                               const Callbacks& callbacks) {
  if (started_) {
    return false;
  }
  std::vector<const char*> caps;
  caps.reserve(capability_count + 1);
  for (std::size_t index = 0; index < capability_count; ++index) {
    caps.push_back(capabilities[index]);
  }
  caps.push_back(nullptr);

  using ResolveFn = const char* (*)(void*);
  using TabKnownFn = int (*)(const char*, void*);
  using ExecuteResult = CrayonAgentHostExecuteResult;
  using ExecuteFn = ExecuteResult (*)(const char*, const char*, const char*,
                                      int (*)(void*), void*, void*);

  CrayonAgentHostConfig config{};
  config.purpose = purpose;
  config.profile = profile;
  config.grant_ttl_ms = grant_ttl_ms;
  config.capabilities = caps.data();
  config.resolve_active_tab_user = callbacks.resolve_active_tab_user;
  config.resolve_active_tab_fn =
      reinterpret_cast<ResolveFn>(callbacks.resolve_active_tab_user);
  config.tab_known_fn = reinterpret_cast<TabKnownFn>(callbacks.tab_known_user);
  config.execute_fn = reinterpret_cast<ExecuteFn>(callbacks.execute_user);
  config.user_data = callbacks.user_data;

  const int status = crayon_agent_host_start(&config);
  started_ = status == kAgentHostOk;
  return started_;
}

bool AgentHostBridgeMac::issue_grant(const char* capability) {
  if (!started_) {
    return false;
  }
  return crayon_agent_host_issue_grant(capability) == kAgentHostOk;
}

void AgentHostBridgeMac::stop() {
  if (!started_) {
    return;
  }
  crayon_agent_host_stop();
  started_ = false;
}

}  // namespace crayon::browser::cef_shell::macos
