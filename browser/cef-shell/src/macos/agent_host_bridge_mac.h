#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_BRIDGE_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_BRIDGE_MAC_H_

// AGT-12Cc2: C++ bridge between the product BrowserApp and the Rust CAAP
// agent-host staticlib (crates/crayon-agent-host). The bridge owns the
// lifecycle only: start with the product's callbacks, stop on teardown.
// Callbacks run on the Rust serve thread; the product marshals CEF work
// onto the UI thread and must keep the callbacks and user data alive
// until stop() returns.

#include <cstddef>
#include <cstdint>

extern "C" {
// Mirrors crayon-agent-host/src/ffi.rs CrayonAgentHostExecuteResult
// (#[repr(C)] { i32, *mut c_char }).
struct CrayonAgentHostExecuteResult {
  int status;
  char* text;
};

// Return status for CrayonAgentHostExecuteFn.
constexpr int kAgentHostExecOk = 0;
constexpr int kAgentHostExecCancelled = 1;
// Statuses >= kAgentHostExecCaapError encode a CaapError at
// (status - kAgentHostExecCaapError); see the gateway error ordering.
constexpr int kAgentHostExecCaapError = 2;

// The product-side trampolines must use C linkage to match the Rust
// `extern "C" fn` ABI (agent_host_bridge follows the same signatures as
// ffi.rs CrayonAgentHostResolveFn/TabKnownFn/ExecuteFn).
typedef const char* (*CrayonAgentHostResolveFn)(void* user);
typedef int (*CrayonAgentHostTabKnownFn)(const char* tab, void* user);
typedef CrayonAgentHostExecuteResult (*CrayonAgentHostExecuteFn)(
    const char* tool, const char* request_json, const char* tab,
    bool (*is_cancelled)(void* user), void* cancel_user, void* user);
// AGT-05C: fires on the serve thread after a client completes its
// handshake; |capabilities| is a comma-separated negotiated wire-name
// list. May be null when the product ignores connect events.
typedef void (*CrayonAgentHostClientConnectedFn)(const char* client,
                                                 const char* capabilities,
                                                 void* user);

// Allocates a host-owned UTF-8 copy of |src| for callback return values;
// the host releases it after marshaling back into Rust.
char* crayon_agent_host_string_alloc(const char* src);
}  // extern "C"

namespace crayon::browser::cef_shell::macos {

class AgentHostBridgeMac final {
 public:
  // Callbacks the product supplies. All run on the Rust serve thread and
  // must be thread-safe (the product marshals CEF work onto TID_UI).
  struct Callbacks {
    CrayonAgentHostResolveFn resolve_active_tab;
    CrayonAgentHostTabKnownFn tab_known;
    CrayonAgentHostExecuteFn execute;
    CrayonAgentHostClientConnectedFn client_connected;
    /// Opaque user data pointer passed back to every callback.
    void* user_data;
  };

  AgentHostBridgeMac();
  ~AgentHostBridgeMac();

  AgentHostBridgeMac(const AgentHostBridgeMac&) = delete;
  AgentHostBridgeMac& operator=(const AgentHostBridgeMac&) = delete;

  /// Starts the host with the given UDS purpose token and capability
  /// allowlist. Returns true on success; false when already running or
  /// the endpoint failed.
  bool start(const char* purpose, const char* profile,
             std::uint64_t grant_ttl_ms, const char* const* capabilities,
             std::size_t capability_count, const Callbacks& callbacks);

  /// Issues a profile-wide grant for the connected client after the
  /// AGT-05 confirmation flow completes.
  bool issue_grant(const char* capability);

  /// Issues a grant bound to the named client (AGT-05C): fails unless
  /// that client is still the active connection, so a stale panel can
  /// never authorize a different client.
  bool issue_grant_for_client(const char* client, const char* capability);

  /// Stops the host. Idempotent. Returns true when the stop completed
  /// (serve thread joined, or the stop was accepted with the thread
  /// detached after a client held the connection open — the ffi
  /// STOP_TIMEOUT case); false when the FFI reported an unexpected state.
  bool stop();

  bool started() const noexcept { return started_; }

 private:
  bool started_ = false;
};

}  // namespace crayon::browser::cef_shell::macos

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_BRIDGE_MAC_H_
