#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crayon::browser_cast_view {

inline constexpr std::size_t kCastSelectionPageSize = 16;
inline constexpr std::size_t kCastSelectionCapacity = 256;
inline constexpr std::size_t kCastSelectionTitleBytes = 128;
inline constexpr std::size_t kCastSelectionIdBytes = 128;
inline constexpr std::size_t kCastSelectionCodeBytes = 16;
inline constexpr std::uint64_t kCastPreparationLifetimeMs = 15000;
inline constexpr std::uint64_t kCastGeometryLifetimeMs = 500;
inline constexpr int kCastOverlayWidthDip = 96;
inline constexpr int kCastOverlayHeightDip = 36;
inline constexpr int kCastOverlayInsetDip = 8;

// Internal UI projection, NOT a wire schema or an authorization. Only a
// Browser/runtime adapter may supply this data after verifying the current
// context. There is intentionally no URL, route override, JS or SDK handle.
struct CastViewContext {
  std::uint64_t browser_session = 0;
  std::string profile_id;
  std::uint32_t tab_id = 0;
  std::uint64_t navigation_id = 0;
  std::uint32_t generation = 0;
  bool operator==(const CastViewContext &other) const;
};

struct CastMediaRef {
  std::uint64_t instance_id = 0;
  std::uint64_t source_revision = 0;
  bool operator==(const CastMediaRef &other) const;
};

struct CastMediaChoice {
  CastMediaRef ref;
  std::string title;
  bool selectable = false;
};

struct CastDeviceChoice {
  std::string id;
  std::string name;
  bool selectable = false;
};

enum class CastDraftPhase {
  kChoosing,
  kConnecting,
  kPreparing,
  kPrepared,
  kCommitting,
  kFailed,
  kExpired
};
enum class CastSelectionRoute { kNone, kDirect, kRelay };

struct CastSelectionSnapshot {
  CastViewContext context;
  std::uint64_t view_revision = 0;
  // True only when the real adapter has negotiated and wired the new protocol.
  bool compatible = false;
  bool picker_open = false;
  std::uint64_t draft_id = 0;
  std::uint64_t draft_revision = 0;
  CastDraftPhase phase = CastDraftPhase::kChoosing;
  CastSelectionRoute route = CastSelectionRoute::kNone;
  std::uint64_t prepared_until_ms = 0;
  bool replacement_confirmation_required = false;
  bool device_connected = false;
  std::uint16_t eligible_count = 0;
  std::uint16_t media_total = 0;
  std::uint16_t media_offset = 0;
  std::vector<CastMediaChoice> media;
  std::uint16_t device_total = 0;
  std::uint16_t device_offset = 0;
  std::vector<CastDeviceChoice> devices;
  // Selected summaries are explicit owner acknowledgements, possibly off-page.
  // Rendering or receiving a new candidate never changes them locally.
  std::optional<CastMediaChoice> selected_media;
  std::optional<CastDeviceChoice> selected_device;
  std::optional<std::uint64_t> session_generation;
  bool playback_paused = false;
};

enum class CastIntentKind {
  kOpen,
  kOpenForMedia,
  kCancel,
  kSelectMedia,
  kSelectDevice,
  kMediaPage,
  kDevicePage,
  kRefreshDevices,
  kLookupCode,
  kConnectDevice,
  kPrepare,
  kConfirmReplacement,
  kCommit,
  kStop,
  kPause,
  kResume
};

struct CastSelectionIntent {
  CastIntentKind kind = CastIntentKind::kOpen;
  CastViewContext context;
  std::uint64_t view_revision = 0;
  std::uint64_t draft_id = 0;
  std::uint64_t draft_revision = 0;
  std::optional<CastMediaRef> media;
  std::string device_id;
  std::string cast_code;
  std::uint16_t page_offset = 0;
  std::optional<std::uint64_t> session_generation;
};

struct CastVideoAnchor {
  CastViewContext context;
  std::uint64_t view_revision = 0;
  CastMediaRef media;
  std::uint64_t expires_at_ms = 0;
  // Only the verified ordinary main-frame adapter may set supported=true.
  // iframe, Shadow DOM, fullscreen, PiP and protected surfaces use false.
  bool supported = false;
  double x = 0, y = 0, width = 0, height = 0;
};
struct CastOverlayBounds {
  int x, y, width, height;
};

// UI-thread presentation/intent filter. It never calls the runtime, creates a
// draft, grants playback, or selects a default. Backend revalidation is
// required even after TakeIntent succeeds. BindContext is a Browser lifecycle
// operation.
class CastSelectionPresentation final {
public:
  void BindContext(CastViewContext context);
  bool Apply(CastSelectionSnapshot snapshot);
  void Clear();
  const std::optional<CastSelectionSnapshot> &snapshot() const {
    return snapshot_;
  }
  bool EntryEnabled() const;
  bool Busy() const;
  bool PickerVisible() const;
  const char *StatusKey(std::uint64_t now_ms) const;
  CastSelectionIntent Intent(CastIntentKind kind) const;
  bool Allows(const CastSelectionIntent &intent, std::uint64_t now_ms) const;
  std::optional<CastSelectionIntent> TakeIntent(CastSelectionIntent intent,
                                                std::uint64_t now_ms);
  std::optional<CastOverlayBounds> PlaceOverlay(const CastVideoAnchor &anchor,
                                                int viewport_width,
                                                int viewport_height,
                                                std::uint64_t now_ms) const;

private:
  const CastMediaChoice *FindMedia(CastMediaRef ref) const;
  bool CompleteSelection() const;
  std::optional<CastViewContext> context_;
  std::optional<CastSelectionSnapshot> snapshot_;
  std::uint64_t last_view_revision_ = 0;
  std::uint64_t sent_draft_id_ = 0;
  std::uint64_t sent_draft_revision_ = 0;
  std::uint64_t cancelled_draft_id_ = 0;
};

} // namespace crayon::browser_cast_view
