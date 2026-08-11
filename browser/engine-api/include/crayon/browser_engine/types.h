#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "crayon/browser_engine/ids.h"
#include "crayon/browser_engine/result.h"

namespace crayon::browser_engine {

inline constexpr std::size_t kMaxBrowserUrlBytes = 2048;
inline constexpr double kMinimumZoomFactor = 0.25;
inline constexpr double kMaximumZoomFactor = 5.0;

class BrowserUrl final {
 public:
  static std::optional<BrowserUrl> TryParse(std::string value);

  const std::string& value() const noexcept { return value_; }

  friend bool operator==(const BrowserUrl& left,
                         const BrowserUrl& right) noexcept {
    return left.value_ == right.value_;
  }

 private:
  explicit BrowserUrl(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

class ZoomFactor final {
 public:
  static std::optional<ZoomFactor> TryCreate(double value) noexcept;

  double value() const noexcept { return value_; }

 private:
  explicit ZoomFactor(double value) noexcept : value_(value) {}

  double value_;
};

enum class ProfileMode { kPersistent = 0, kPrivate };
enum class PermissionKind {
  kCamera = 0,
  kMicrophone,
  kNotifications,
  kGeolocation,
  kClipboardRead,
  kClipboardWrite,
  kDownload,
};
enum class PermissionDecision { kAllowOnce = 0, kAllowForProfile, kDeny };
enum class TrustedInputKind { kKeyboard = 0, kMouse, kTouch };
enum class ObservationTopic {
  kNavigation = 0,
  kMedia,
  kNetworkMetadata,
  kTrustedInput
};
enum class ObservationKind {
  kDocumentReady = 0,
  kMediaActivity,
  kNetworkActivity,
  kTrustedInput
};
enum class ProfileEventKind { kCreated = 0, kDestroyed };
enum class TabEventKind { kCreated = 0, kClosed };
enum class NavigationEventKind {
  kStarted = 0,
  kCommitted,
  kCompleted,
  kFailed
};

bool IsValid(ProfileMode value) noexcept;
bool IsValid(PermissionKind value) noexcept;
bool IsValid(PermissionDecision value) noexcept;
bool IsValid(TrustedInputKind value) noexcept;
bool IsValid(ObservationTopic value) noexcept;
bool IsValid(ObservationKind value) noexcept;

struct ProfileConfig final {
  ProfileId profile_id;
  ProfileMode mode;
};

struct TabCreateRequest final {
  ProfileId profile_id;
  TabId tab_id;
  std::optional<BrowserUrl> initial_url;
};

struct NavigationRequest final {
  TabId tab_id;
  BrowserUrl url;
};

struct PermissionResolution final {
  PermissionRequestId request_id;
  PermissionDecision decision;
};

struct ObservationSubscription final {
  SubscriptionId subscription_id;
  TabId tab_id;
  ObservationTopic topic;
};

struct ProfileEvent final {
  ProfileEventKind kind;
  ProfileId profile_id;
};

struct TabEvent final {
  TabEventKind kind;
  ProfileId profile_id;
  TabId tab_id;
};

struct NavigationEvent final {
  NavigationEventKind kind;
  TabId tab_id;
  NavigationId navigation_id;
  BrowserUrl url;
  EngineErrorCode error;
};

struct PermissionRequest final {
  PermissionRequestId request_id;
  TabId tab_id;
  NavigationId navigation_id;
  PermissionKind permission;
};

struct TrustedInputFact final {
  TabId tab_id;
  NavigationId navigation_id;
  TrustedInputKind kind;
  std::uint64_t sequence;
};

struct ObservationEvent final {
  SubscriptionId subscription_id;
  TabId tab_id;
  NavigationId navigation_id;
  ObservationKind kind;
};

}  // namespace crayon::browser_engine
