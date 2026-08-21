#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

#include "crayon/browser_navigation/site_identity.h"

namespace crayon::browser_site_controls {

/// Capacity and length bounds.
inline constexpr std::size_t kMaxPermissionEntries = 256;
inline constexpr std::size_t kMaxProtocolMemoryEntries = 256;
inline constexpr std::size_t kMaxOriginLength = 256;
inline constexpr std::size_t kMaxSchemeLength = 64;

namespace detail {

/// Shape check for pre-extracted origins (scheme://host[:port]).
/// The CEF-05 site-origin helper performs the real extraction; this
/// module only re-checks shape and bounds.
inline bool IsValidSiteOrigin(const std::string& origin) noexcept {
  if (origin.empty() || origin.size() > kMaxOriginLength) {
    return false;
  }
  const bool known_scheme = origin.rfind("https://", 0) == 0 ||
                            origin.rfind("http://", 0) == 0 ||
                            origin.rfind("crayon://", 0) == 0;
  if (!known_scheme) {
    return false;
  }
  for (const char c : origin) {
    const unsigned char uc = static_cast<unsigned char>(c);
    // '|' is the internal key separator; control chars and '@' are
    // rejected to keep origins unambiguous and log-safe.
    if (uc < 0x20 || uc == 0x7F || c == '@' || c == '|') {
      return false;
    }
  }
  return true;
}

}  // namespace detail

/// Who is writing security-UI state.  Page content can never set it.
enum class ControlSource {
  kEngine = 0,
  kPageContent,
};

/// Closed permission decisions for a (origin, kind) pair.
enum class SitePermission {
  kDeny = 0,
  kAllowSession,
  /// Allowed until a caller-injected expiry timestamp.
  kAllowUntil,
};

constexpr bool IsValid(SitePermission decision) noexcept {
  switch (decision) {
    case SitePermission::kDeny:
    case SitePermission::kAllowSession:
    case SitePermission::kAllowUntil:
      return true;
  }
  return false;
}

/// Closed permission kinds (mirrors CEF-05 coverage).
enum class PermissionKind {
  kCamera = 0,
  kMicrophone,
  kNotifications,
  kGeolocation,
  kClipboardRead,
  kClipboardWrite,
  kDownload,
};

constexpr bool IsValid(PermissionKind kind) noexcept {
  switch (kind) {
    case PermissionKind::kCamera:
    case PermissionKind::kMicrophone:
    case PermissionKind::kNotifications:
    case PermissionKind::kGeolocation:
    case PermissionKind::kClipboardRead:
    case PermissionKind::kClipboardWrite:
    case PermissionKind::kDownload:
      return true;
  }
  return false;
}

/// Closed certificate error kinds shown by the interstitial model.
enum class CertErrorKind {
  kExpired = 0,
  kNameMismatch,
  kUntrusted,
  kGeneric,
};

constexpr bool IsValid(CertErrorKind kind) noexcept {
  switch (kind) {
    case CertErrorKind::kExpired:
    case CertErrorKind::kNameMismatch:
    case CertErrorKind::kUntrusted:
    case CertErrorKind::kGeneric:
      return true;
  }
  return false;
}

/// Closed user decisions on a certificate error.
enum class CertDecision {
  kGoBack = 0,
  /// Proceed once; bound to the navigation generation it was granted on.
  kProceedOnce,
};

/// Closed decisions for external-protocol launch requests.
enum class ProtocolDecision {
  kDeny = 0,
  kAllowOnce,
  kRememberAllow,
  kRememberDeny,
};

constexpr bool IsValid(ProtocolDecision decision) noexcept {
  switch (decision) {
    case ProtocolDecision::kDeny:
    case ProtocolDecision::kAllowOnce:
    case ProtocolDecision::kRememberAllow:
    case ProtocolDecision::kRememberDeny:
      return true;
  }
  return false;
}

/// Command failure.  Stable variants carry no site data.
enum class SiteControlError {
  /// A page-content source tried to write security state.
  kForgedSource = 0,
  kInvalidInput,
  kUnknownEntry,
};

/// Per-origin site-controls state machine.
///
/// All timestamps are caller-injected seconds; the module never reads a
/// clock.  Thread contract: single-threaded, UI thread only.
class SiteControlsStateMachine final {
 public:
  SiteControlsStateMachine() = default;

  // --- Security identity (engine-only writes) ---

  bool SetSiteIdentity(crayon::browser_navigation::SiteIdentity identity,
                       ControlSource source) noexcept;
  crayon::browser_navigation::SiteIdentity site_identity() const noexcept {
    return identity_;
  }

  // --- Permission entries with TTL ---

  /// Records a decision for (origin, kind).  `expires_at` is required for
  /// `kAllowUntil` and must lie in the future relative to `now`.
  bool SetPermission(const std::string& origin,
                     PermissionKind kind,
                     SitePermission decision,
                     std::uint64_t now,
                     std::uint64_t expires_at,
                     SiteControlError* error = nullptr);

  /// Removes a stored decision.
  bool ClearPermission(const std::string& origin, PermissionKind kind);

  /// Effective decision at `now`; expired entries count as `kDeny`.
  SitePermission PermissionAt(const std::string& origin,
                              PermissionKind kind,
                              std::uint64_t now) const noexcept;

  std::size_t permission_entry_count() const noexcept {
    return permissions_.size();
  }

  // --- Certificate errors ---

  bool OnCertificateError(CertErrorKind kind,
                          std::uint64_t navigation_generation,
                          ControlSource source) noexcept;
  bool DecideCertificateError(CertDecision decision) noexcept;
  bool HasPendingCertificateError() const noexcept {
    return pending_cert_error_.has_value();
  }
  /// True only for the navigation generation the proceed was granted on.
  bool ProceedOnceApplies(std::uint64_t navigation_generation) const noexcept;

  // --- External protocol confirmation ---

  /// Decides an external-protocol launch.  Dangerous schemes are rejected
  /// without ever entering the confirmation flow.  Remember decisions are
  /// stored per (scheme, origin) with bounded FIFO eviction.
  bool DecideExternalProtocol(const std::string& scheme,
                              const std::string& origin,
                              ProtocolDecision decision,
                              SiteControlError* error = nullptr);

  /// A remembered decision for (scheme, origin), if any.
  std::optional<ProtocolDecision> RememberedProtocolDecision(
      const std::string& scheme,
      const std::string& origin) const;

  std::size_t protocol_memory_count() const noexcept {
    return protocol_memory_.size();
  }

  /// Clears all state and rejects every subsequent command.
  void Shutdown() noexcept;

  bool active() const noexcept { return active_; }

 private:
  struct PermissionEntry final {
    SitePermission decision = SitePermission::kDeny;
    std::uint64_t expires_at = 0;
    std::uint64_t recorded_at = 0;
  };

  struct PendingCertError final {
    CertErrorKind kind = CertErrorKind::kGeneric;
    std::uint64_t navigation_generation = 0;
    bool proceed_once = false;
  };

  static bool IsDangerousScheme(const std::string& scheme) noexcept;
  static std::string PermissionKey(const std::string& origin,
                                   PermissionKind kind);

  crayon::browser_navigation::SiteIdentity identity_ =
      crayon::browser_navigation::SiteIdentity::kUnknown;
  std::unordered_map<std::string, PermissionEntry> permissions_;
  std::deque<std::string> permission_recency_;  // Oldest first.
  std::optional<PendingCertError> pending_cert_error_;
  std::unordered_map<std::string, ProtocolDecision> protocol_memory_;
  std::deque<std::string> protocol_recency_;  // Oldest first.
  bool active_ = true;
};

}  // namespace crayon::browser_site_controls
