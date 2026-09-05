#pragma once

#include <cstdint>
#include <optional>

#include "crayon/browser_engine/ids.h"
#include "crayon/browser_engine/result.h"

namespace crayon::browser_engine {

class MountEpoch final {
public:
  static constexpr std::optional<MountEpoch>
  TryCreate(std::uint64_t value) noexcept {
    return value == 0 ? std::nullopt
                      : std::optional<MountEpoch>(MountEpoch(value));
  }

  constexpr std::uint64_t value() const noexcept { return value_; }

  friend constexpr bool operator==(MountEpoch left, MountEpoch right) noexcept {
    return left.value_ == right.value_;
  }

  friend constexpr bool operator!=(MountEpoch left, MountEpoch right) noexcept {
    return !(left == right);
  }

  friend constexpr bool operator<(MountEpoch left, MountEpoch right) noexcept {
    return left.value_ < right.value_;
  }

private:
  explicit constexpr MountEpoch(std::uint64_t value) noexcept : value_(value) {}

  std::uint64_t value_;
};

enum class ContentPurpose {
  kWeb = 0,
  kControlledBuiltIn,
  kLocalDocument,
};

enum class ContentCapability {
  kNavigate = 0,
  kSnapshot,
  kTrustedInput,
  kMediaObservation,
  kZoom,
  kFind,
};

bool IsValid(ContentPurpose value) noexcept;
bool IsValid(ContentCapability value) noexcept;

class ContentCapabilitySet final {
public:
  static constexpr std::uint32_t kKnownMask = 0x3fU;

  static constexpr std::optional<ContentCapabilitySet>
  TryCreate(std::uint32_t bits) noexcept {
    return (bits & ~kKnownMask) == 0
               ? std::optional<ContentCapabilitySet>(ContentCapabilitySet(bits))
               : std::nullopt;
  }

  static constexpr ContentCapabilitySet None() noexcept {
    return ContentCapabilitySet(0);
  }

  constexpr std::uint32_t bits() const noexcept { return bits_; }
  bool Supports(ContentCapability capability) const noexcept;

  friend constexpr bool operator==(ContentCapabilitySet left,
                                   ContentCapabilitySet right) noexcept {
    return left.bits_ == right.bits_;
  }

  friend constexpr bool operator!=(ContentCapabilitySet left,
                                   ContentCapabilitySet right) noexcept {
    return !(left == right);
  }

private:
  explicit constexpr ContentCapabilitySet(std::uint32_t bits) noexcept
      : bits_(bits) {}

  std::uint32_t bits_;
};

struct ContentViewMountRequest final {
  ProfileId profile_id;
  TabId tab_id;
  NavigationId navigation_id;
  MountEpoch mount_epoch;
  ContentPurpose purpose;
};

enum class ContentViewResultKind {
  kCreated = 0,
  kCreateFailed,
  kCloseCancelled,
  kClosed,
  kCrashed,
};

struct ContentViewResult final {
  ContentViewMountRequest mount;
  ContentViewResultKind kind;
  ContentCapabilitySet capabilities;
  EngineErrorCode error;
};

bool IsValid(ContentViewResultKind value) noexcept;
bool IsValid(const ContentViewMountRequest &request) noexcept;
bool IsValid(const ContentViewResult &result) noexcept;

} // namespace crayon::browser_engine
