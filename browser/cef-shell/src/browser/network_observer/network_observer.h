// CEF-11: resource request/response observation guard.
//
// Normalizes network observations into a closed DTO field set with
// size and rate bounds; sensitive headers and bodies never enter the
// DTO (closed header allowlist, presence flags only).  EME `encrypted`
// signals associate with clear candidates to upgrade protection
// (BR-011); blob:/MediaStream candidates surface as unsupported
// without fabricated URLs (BR-012).
//
// Thread contract: single-threaded (browser IO thread), injected clock.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crayon::cef_shell::network {

/// Maximum URL length in observations, in bytes.
inline constexpr std::size_t kMaxUrlLen = 2'048;
/// Maximum retained observations (bounded store).
inline constexpr std::size_t kMaxObservations = 128;
/// Maximum accepted observations per rate window.
inline constexpr std::uint32_t kRateWindowBudget = 256;
/// Rate window length in milliseconds.
inline constexpr std::uint64_t kRateWindowMs = 1'000;

/// Closed resource kinds.
enum class ResourceKind { kMedia = 0, kManifest, kSegment, kDocument, kOther };

/// Closed header classes that may be *observed as present* — the header
/// value itself never enters the DTO (BR-008: no content leakage).
enum class HeaderClass { kNone = 0, kReferer, kUserAgent, kRange, kAuthorization };

/// Closed observation outcome.
enum class NetworkObserveResult {
  kAccepted = 0,
  kDroppedInvalidUrl,
  kDroppedSensitiveHeader,
  kDroppedOversize,
  kDroppedRateLimited,
  kDroppedCapacity,
};

/// One normalized network observation DTO.
struct NetworkObservation {
  std::uint64_t navigation_id = 0;
  std::string url;             // classified http(s)/blob only
  ResourceKind kind = ResourceKind::kOther;
  HeaderClass header_class = HeaderClass::kNone;
  std::uint64_t content_length = 0;
  bool eme_encrypted = false;  // BR-011 protection upgrade signal
};

/// Observation store with size/rate policing.
class NetworkObserver final {
 public:
  /// Classifies a URL for observation; empty/false for malformed,
  /// oversize or dangerous schemes.
  static bool ClassifyUrl(const std::string& url, std::string* normalized,
                          bool* is_blob);

  /// Reports whether `header_name` (ASCII lowercase) is in the closed
  /// observable set; the value is never taken.
  static bool IsObservableHeader(const std::string& lowercase_name,
                                 HeaderClass* out_class);

  /// Ingests one observation; `sensitive_header_present` describes a
  /// request that carried Cookie/Authorization — the DTO records only
  /// the closed class, and non-observable sensitive headers reject the
  /// observation rather than leaking a value.
  NetworkObserveResult Observe(NetworkObservation observation,
                               const std::string& present_header_name,
                               std::uint64_t now_ms);

  /// Marks that an EME `encrypted` event fired for `navigation_id`
  /// (BR-011): every retained media/manifest observation for that
  /// navigation upgrades its protection signal.
  void AssociateEmeEncrypted(std::uint64_t navigation_id);

  /// Drains retained observations (oldest first).
  std::vector<NetworkObservation> Drain();

  std::size_t retained_count() const { return observations_.size(); }

 private:
  std::vector<NetworkObservation> observations_;
  std::uint64_t window_start_ms_ = 0;
  std::uint32_t window_used_ = 0;
};

}  // namespace crayon::cef_shell::network
