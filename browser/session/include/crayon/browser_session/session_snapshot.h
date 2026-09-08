#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace crayon::browser_session {

inline constexpr std::size_t kMaxSessionBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxSessionUrlBytes = 8192;
inline constexpr std::size_t kMaxSessionGroupBytes = 64;
inline constexpr std::size_t kMaxSessionGroupsPerWindow = 8;

struct SessionTabSnapshot final {
  std::string url;
  bool pinned = false;
  bool muted = false;
  std::optional<std::string> group = std::nullopt;
};

struct SessionWindowSnapshot final {
  std::string window_id;
  std::vector<SessionTabSnapshot> tabs;
  std::size_t active_index = 0;
};

struct SessionProfileSnapshot final {
  std::string profile_id;
  std::vector<SessionWindowSnapshot> windows;
};

enum class SessionSnapshotError {
  kNone = 0,
  kTooLarge,
  kUnsupportedVersion,
  kMalformed,
  kInvalidValue,
  kDuplicateIdentity,
  kCapacityExceeded,
};

bool IsValid(const SessionTabSnapshot &tab) noexcept;
bool IsValid(const SessionWindowSnapshot &window) noexcept;
bool IsValid(const SessionProfileSnapshot &profile) noexcept;

std::optional<std::string>
EncodeSessionSnapshotV2(const SessionProfileSnapshot &profile,
                        SessionSnapshotError *error = nullptr);
std::optional<SessionProfileSnapshot>
DecodeSessionSnapshot(const std::string &encoded,
                      SessionSnapshotError *error = nullptr);

} // namespace crayon::browser_session
