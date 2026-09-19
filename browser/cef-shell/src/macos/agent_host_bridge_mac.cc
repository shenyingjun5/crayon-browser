// AGT-12Cc2: C++ adapter for the Rust agent-host staticlib. Every Rust
// FFI entry point is panic-free; this file maps the C ABI onto a small
// RAII bridge the product owns. The tool callbacks pass through as
// function pointers — the product (BrowserApp) provides the actual
// implementations and marshals any CEF work onto the UI thread.

#include "macos/agent_host_bridge_mac.h"

#include <cstddef>
#include <vector>

// Rust-side config layout (ffi.rs CrayonAgentHostConfig, #[repr(C)]).
struct CrayonAgentHostConfig {
  const char* purpose;
  const char* profile;
  std::uint64_t grant_ttl_ms;
  const char* const* capabilities;
  CrayonAgentHostResolveFn resolve_active_tab_fn;
  CrayonAgentHostTabKnownFn tab_known_fn;
  CrayonAgentHostExecuteFn execute_fn;
  void* user_data;
  CrayonAgentHostClientConnectedFn client_connected_fn;
};

// Pin the mirrored ABI layout so a one-sided ffi.rs change breaks the
// build instead of corrupting the call boundary. The tail assert pins
// the full field count: a Rust-side appended field must be mirrored.
static_assert(offsetof(CrayonAgentHostConfig, purpose) == 0);
static_assert(offsetof(CrayonAgentHostConfig, profile) ==
              offsetof(CrayonAgentHostConfig, purpose) + sizeof(const char*));
static_assert(offsetof(CrayonAgentHostConfig, grant_ttl_ms) ==
              offsetof(CrayonAgentHostConfig, profile) + sizeof(const char*));
static_assert(
    offsetof(CrayonAgentHostConfig, capabilities) ==
    offsetof(CrayonAgentHostConfig, grant_ttl_ms) + sizeof(std::uint64_t));
static_assert(offsetof(CrayonAgentHostConfig, resolve_active_tab_fn) ==
              offsetof(CrayonAgentHostConfig, capabilities) +
                  sizeof(const char* const*));
static_assert(offsetof(CrayonAgentHostConfig, tab_known_fn) ==
              offsetof(CrayonAgentHostConfig, resolve_active_tab_fn) +
                  sizeof(void*));
static_assert(offsetof(CrayonAgentHostConfig, execute_fn) ==
              offsetof(CrayonAgentHostConfig, tab_known_fn) + sizeof(void*));
static_assert(offsetof(CrayonAgentHostConfig, user_data) ==
              offsetof(CrayonAgentHostConfig, execute_fn) + sizeof(void*));
static_assert(offsetof(CrayonAgentHostConfig, client_connected_fn) ==
              offsetof(CrayonAgentHostConfig, user_data) + sizeof(void*));

extern "C" {
// crayon-agent-host staticlib (crates/crayon-agent-host/src/ffi.rs).
int crayon_agent_host_start(const CrayonAgentHostConfig* config);
int crayon_agent_host_stop();
int crayon_agent_host_issue_grant(const char* capability);
int crayon_agent_host_issue_grant_for_client(const char* client,
                                             const char* capability);

// Status codes mirrored from ffi.rs.
constexpr int kAgentHostOk = 0;
// ffi.rs CRAYON_AGENT_HOST_STOP_TIMEOUT: the serve thread stayed blocked
// on a client connection and was detached at the join timeout.
constexpr int kAgentHostStopTimedOut = 8;
}  // extern "C"

namespace crayon::browser::cef_shell::macos {

AgentHostBridgeMac::AgentHostBridgeMac() = default;

AgentHostBridgeMac::~AgentHostBridgeMac() { stop(); }

bool AgentHostBridgeMac::start(const char* purpose, const char* profile,
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

  CrayonAgentHostConfig config{};
  config.purpose = purpose;
  config.profile = profile;
  config.grant_ttl_ms = grant_ttl_ms;
  config.capabilities = caps.data();
  config.resolve_active_tab_fn = callbacks.resolve_active_tab;
  config.tab_known_fn = callbacks.tab_known;
  config.execute_fn = callbacks.execute;
  config.user_data = callbacks.user_data;
  config.client_connected_fn = callbacks.client_connected;

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

bool AgentHostBridgeMac::issue_grant_for_client(const char* client,
                                                const char* capability) {
  if (!started_) {
    return false;
  }
  return crayon_agent_host_issue_grant_for_client(client, capability) ==
         kAgentHostOk;
}

bool AgentHostBridgeMac::stop() {
  if (!started_) {
    return true;
  }
  const int status = crayon_agent_host_stop();
  started_ = false;
  // STOP_TIMEOUT reports a serve thread detached while blocked on a live
  // client: nothing can join it here, but the caller must not mistake the
  // stop for a clean join.
  return status == kAgentHostOk || status == kAgentHostStopTimedOut;
}

}  // namespace crayon::browser::cef_shell::macos
