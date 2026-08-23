// CEF-09: renderer-side media observation model (platform-neutral).
//
// Normalizes media events, visibility and frame/navigation identity
// into closed observation records, with no auto-interaction: this
// module never synthesizes clicks, seeks, rate changes or ad-domain
// filtering (BR-009/BR-010).  Stale navigation/frame identity is
// dropped at ingestion (BR-007), observer teardown prevents late
// events from rebuilding candidates (BR-013), and blob:/MediaStream
// sources without an underlying URL are surfaced as such — never
// fabricated into a direct-cast URL (BR-012).  Page-reported events
// are tagged untrusted; the browser-side gate lives in CEF-10.
//
// Thread contract: single-threaded (renderer main).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crayon::cef_shell::renderer {

/// Maximum URL length accepted in observations, in bytes.
inline constexpr std::size_t kMaxSourceUrlLen = 2'048;
/// Maximum concurrent tracked media elements per frame.
inline constexpr std::size_t kMaxMediaElements = 16;

/// Closed media element playback states.
enum class MediaPlaybackState { kIdle = 0, kPlaying, kPaused, kEnded };

/// Closed source kinds (BR-012: blob/MediaStream carry no castable
/// URL and must not be fabricated).
enum class MediaSourceKind { kHttpUrl = 0, kBlobUrl, kMediaStream, kUnknown };

/// One normalized media observation.  All fields derive from renderer
/// facts; nothing here is trusted for authorization.
struct MediaObservation {
  std::uint64_t navigation_id = 0;
  std::uint64_t frame_id = 0;
  std::uint32_t element_id = 0;
  MediaPlaybackState playback = MediaPlaybackState::kIdle;
  MediaSourceKind source_kind = MediaSourceKind::kUnknown;
  std::string source_url;           // empty for blob/stream kinds
  double visible_fraction = 0.0;    // [0,1] viewport intersection
  double current_time_seconds = 0;  // page-reported, untrusted
  bool has_user_gesture = false;    // page-reported, untrusted
};

/// Classified observation outcome.
enum class ObserveResult {
  kAccepted = 0,
  kDroppedStaleNavigation,
  kDroppedCapacity,
  kDroppedTeardown,
  kDroppedInvalidUrl,
};

/// Per-frame observation aggregator.
class MediaObserver final {
 public:
  explicit MediaObserver(std::uint64_t frame_id) : frame_id_(frame_id) {}

  /// Advances the navigation identity; observations carrying older ids
  /// are rejected (BR-007).
  void AdvanceNavigation(std::uint64_t navigation_id);

  /// Ingests one observation; returns the classification.  Oversize or
  /// malformed URLs are dropped; blob/stream sources must not carry a
  /// fabricated URL.
  ObserveResult Observe(MediaObservation observation);

  /// Tears the observer down; every further observation is dropped
  /// (BR-013: late events cannot rebuild candidates).
  void TearDown();

  /// Reports whether an element would currently satisfy the
  /// visibility/playback preconditions the browser-side gate
  /// cross-checks (CEF-10 re-verifies with trusted input).
  std::optional<MediaObservation> FindEligible(std::uint64_t navigation_id) const;

  std::uint64_t navigation_id() const { return navigation_id_; }
  std::size_t tracked_count() const { return elements_.size(); }
  bool torn_down() const { return torn_down_; }

 private:
  std::uint64_t frame_id_ = 0;
  std::uint64_t navigation_id_ = 0;
  bool torn_down_ = false;
  std::vector<MediaObservation> elements_;
};

/// Classifies a source URL into the closed kind set; oversize or
/// malformed inputs yield kUnknown with an empty normalized URL.
MediaSourceKind ClassifySourceUrl(const std::string& url, std::string* normalized);

}  // namespace crayon::cef_shell::renderer
