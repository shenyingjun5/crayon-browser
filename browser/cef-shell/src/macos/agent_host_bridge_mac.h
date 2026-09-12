#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_BRIDGE_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_BRIDGE_MAC_H_

// AGT-12Cc2: C++ bridge between the product BrowserApp and the Rust CAAP
// agent-host staticlib (crates/crayon-agent-host). The bridge owns the
// lifecycle only: start with the product's callbacks, stop on teardown.
// Tool execution and grant issuance callbacks run on the serve thread and
// must not touch CEF objects directly — the host marshals them onto the
// CEF UI thread via CefPostTask.

#include <cstddef>
#include <cstdint>

namespace crayon::browser::cef_shell::macos {

class AgentHostBridgeMac final {
 public:
  // Callbacks the product supplies. All run on the Rust serve thread and
  // must be thread-safe (the product marshals CEF work onto TID_UI).
  struct Callbacks {
    /// Resolves the active tab id (UTF-8, NUL-terminated) or returns
    /// nullptr when no tab exists. The returned string must be allocated
    /// by crayon_agent_host_string_alloc.
    void* resolve_active_tab_user;
    /// Validates a targeted tab id. Returns 0 when known.
    void* tab_known_user;
    /// Executes one confirmed, authorized tool.
    void* execute_user;
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
  bool start(const char* purpose,
             const char* profile,
             std::uint64_t grant_ttl_ms,
             const char* const* capabilities,
             std::size_t capability_count,
             const Callbacks& callbacks);

  /// Issues a profile-wide grant for the connected client after the
  /// AGT-05 confirmation flow completes.
  bool issue_grant(const char* capability);

  /// Stops the host. Idempotent.
  void stop();

  bool started() const noexcept { return started_; }

 private:
  bool started_ = false;
};

}  // namespace crayon::browser::cef_shell::macos

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_BRIDGE_MAC_H_
