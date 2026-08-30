#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace crayon::browser_engine {

inline constexpr std::size_t kMaxOpaqueIdBytes = 128;

bool IsValidOpaqueId(std::string_view value) noexcept;

template <typename Tag>
class OpaqueId final {
 public:
  static std::optional<OpaqueId> TryCreate(std::string value) {
    if (!IsValidOpaqueId(value)) {
      return std::nullopt;
    }
    return OpaqueId(std::move(value));
  }

  const std::string& value() const noexcept { return value_; }

  friend bool operator==(const OpaqueId& left, const OpaqueId& right) noexcept {
    return left.value_ == right.value_;
  }

  friend bool operator!=(const OpaqueId& left, const OpaqueId& right) noexcept {
    return !(left == right);
  }

  friend bool operator<(const OpaqueId& left, const OpaqueId& right) noexcept {
    return left.value_ < right.value_;
  }

 private:
  explicit OpaqueId(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

struct ProfileIdTag;
struct TabIdTag;
struct PermissionRequestIdTag;
struct SubscriptionIdTag;
struct SnapshotRequestIdTag;
struct DiscoveryTargetIdTag;

using ProfileId = OpaqueId<ProfileIdTag>;
using TabId = OpaqueId<TabIdTag>;
using PermissionRequestId = OpaqueId<PermissionRequestIdTag>;
using SubscriptionId = OpaqueId<SubscriptionIdTag>;
using SnapshotRequestId = OpaqueId<SnapshotRequestIdTag>;
using DiscoveryTargetId = OpaqueId<DiscoveryTargetIdTag>;

class NavigationId final {
 public:
  static constexpr NavigationId FromRaw(std::uint64_t value) noexcept {
    return NavigationId(value);
  }

  constexpr std::uint64_t value() const noexcept { return value_; }

  friend constexpr bool operator==(NavigationId left,
                                   NavigationId right) noexcept {
    return left.value_ == right.value_;
  }

  friend constexpr bool operator!=(NavigationId left,
                                   NavigationId right) noexcept {
    return !(left == right);
  }

 private:
  explicit constexpr NavigationId(std::uint64_t value) noexcept
      : value_(value) {}

  std::uint64_t value_;
};

}  // namespace crayon::browser_engine
