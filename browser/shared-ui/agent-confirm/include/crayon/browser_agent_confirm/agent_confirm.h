// AGT-05: agent tool-call confirmation view model (AG-004).
//
// Presents one R2–R4 confirmation with closed fields — client, tool,
// risk, target scope (the grant scope_summary form), a parameter
// digest that never carries raw values, and a data-disclosure flag —
// and a Confirm/Deny flow where any context change (navigation,
// device, params fingerprint) invalidates the pending state and
// forces re-presentation.  Expiry uses the injected clock; nothing can
// confirm an expired or stale request.
//
// Accessibility: every field maps to a locale label key; the parity
// contract test pins the key set.
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crayon::browser_agent_confirm {

/// Maximum lengths for presentation fields, in bytes.
inline constexpr std::size_t kMaxFieldLen = 128;
inline constexpr std::size_t kMaxScopeLen = 256;
inline constexpr std::size_t kMaxParams = 16;
inline constexpr std::size_t kMaxParamKeyLen = 32;
/// Default confirmation validity window, in milliseconds.
inline constexpr std::uint64_t kConfirmTtlMs = 60'000;

/// Closed parameter sensitivity classes driving masking.
enum class ParamSensitivity { kPlain = 0, kSensitive };

/// One parameter digest entry: key plus bounded metadata only.  The
/// raw value never enters this model.
struct ParamDigest {
  std::string key;
  std::size_t value_len = 0;
  ParamSensitivity sensitivity = ParamSensitivity::kPlain;
};

/// Reports whether `key` is in the sensitive set (password/payment/
/// cookie/token/file families) and must be fully masked.
bool IsSensitiveParamKey(const std::string& key);

/// Validates a closed token field for presentation.
bool IsValidToken(const std::string& value);

/// The presented request (immutable once built).
struct AgentConfirmRequest {
  std::string client;
  std::string tool;
  std::string capability;      // wire name, e.g. "navigation"
  std::string risk;            // wire name, e.g. "r3"
  std::string target_scope;    // grant scope_summary form
  std::vector<ParamDigest> params;
  bool discloses_page_data = false;
  std::uint64_t expires_at_ms = 0;

  /// Presentation digest: stable string over the identity + params so
  /// the caller can detect context changes without storing raw values.
  [[nodiscard]] std::string Fingerprint() const;
};

/// Confirmation lifecycle states.
enum class ConfirmState {
  kNone = 0,
  kPending,
  kConfirmed,
  kDenied,
};

class AgentConfirmModel final {
 public:
  /// Presents a request; validates bounds/tokens and stamps the
  /// fingerprint.  Replaces any pending request (old one dies).
  bool Present(const AgentConfirmRequest& request, std::uint64_t now_ms);

  /// Confirms the pending request before expiry; anything else is a
  /// stable rejection.
  bool Confirm(std::uint64_t now_ms);

  /// Denies the pending request (terminal until re-presented).
  bool Deny();

  /// Context change hook (navigation/device/params).  A pending or
  /// confirmed request whose fingerprint differs is invalidated: the
  /// UI must re-present before any further action.
  void OnContextChanged(const std::string& new_fingerprint);

  /// Marks expiry proactively (clock tick); expired pending requests
  /// cannot be confirmed afterwards.
  void Tick(std::uint64_t now_ms);

  ConfirmState state() const { return state_; }
  bool stale() const { return stale_; }
  const AgentConfirmRequest* request() const { return &request_; }

 private:
  ConfirmState state_ = ConfirmState::kNone;
  bool stale_ = false;
  std::uint64_t expires_at_ms_ = 0;
  std::string fingerprint_;
  AgentConfirmRequest request_;
};

}  // namespace crayon::browser_agent_confirm
