#include "crayon/browser_session/session_snapshot.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "crayon/browser_session/session_restore.h"

namespace crayon::browser_session {
namespace {

constexpr std::string_view kV1Header = "CRAYON_SESSION_V1";
constexpr std::string_view kV2Header = "CRAYON_SESSION_V2";
constexpr std::string_view kFallbackUrl = "crayon://newtab/";

void SetError(SessionSnapshotError *out, SessionSnapshotError value) noexcept {
  if (out) {
    *out = value;
  }
}

bool HasControl(std::string_view value) noexcept {
  return std::any_of(value.begin(), value.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x20 || byte == 0x7f;
  });
}

bool StartsWithAsciiCase(std::string_view value,
                         std::string_view prefix) noexcept {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    char left = value[index];
    char right = prefix[index];
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
  }
  return true;
}

bool IsAllowedUrl(std::string_view url) noexcept {
  if (url.empty() || url.size() > kMaxSessionUrlBytes || HasControl(url)) {
    return false;
  }
  if (url == "crayon://newtab/" ||
      StartsWithAsciiCase(url, "crayon://mdv/")) {
    return true;
  }
  std::size_t scheme = 0;
  if (StartsWithAsciiCase(url, "https://")) {
    scheme = 8;
  } else if (StartsWithAsciiCase(url, "http://")) {
    scheme = 7;
  } else {
    return false;
  }
  const std::size_t authority_end = url.find_first_of("/?#", scheme);
  const auto authority = url.substr(
      scheme, authority_end == std::string_view::npos
                  ? std::string_view::npos
                  : authority_end - scheme);
  return !authority.empty() && authority.find('@') == std::string_view::npos &&
         authority.find(' ') == std::string_view::npos;
}

char HexDigit(unsigned value) {
  return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

std::string HexEncode(std::string_view value) {
  std::string encoded;
  encoded.reserve(value.size() * 2);
  for (unsigned char byte : value) {
    encoded.push_back(HexDigit(byte >> 4));
    encoded.push_back(HexDigit(byte & 0x0f));
  }
  return encoded;
}

std::optional<unsigned> HexValue(char value) {
  if (value >= '0' && value <= '9') {
    return static_cast<unsigned>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<unsigned>(value - 'a' + 10);
  }
  return std::nullopt;
}

std::optional<std::string> HexDecode(std::string_view value) {
  if (value.size() % 2 != 0) {
    return std::nullopt;
  }
  std::string decoded;
  decoded.reserve(value.size() / 2);
  for (std::size_t index = 0; index < value.size(); index += 2) {
    const auto high = HexValue(value[index]);
    const auto low = HexValue(value[index + 1]);
    if (!high || !low) {
      return std::nullopt;
    }
    decoded.push_back(static_cast<char>((*high << 4) | *low));
  }
  return decoded;
}

std::vector<std::string_view> Fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= line.size()) {
    const std::size_t end = line.find('\t', begin);
    fields.push_back(line.substr(begin, end == std::string_view::npos
                                           ? std::string_view::npos
                                           : end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return fields;
}

std::optional<std::size_t> ParseSize(std::string_view value) {
  std::size_t parsed = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size()
             ? std::optional<std::size_t>(parsed)
             : std::nullopt;
}

std::vector<std::string_view> Lines(const std::string &encoded) {
  std::vector<std::string_view> lines;
  std::string_view input(encoded);
  std::size_t begin = 0;
  while (begin < input.size()) {
    const std::size_t end = input.find('\n', begin);
    auto line = input.substr(begin, end == std::string_view::npos
                                       ? std::string_view::npos
                                       : end - begin);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    lines.push_back(line);
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return lines;
}

} // namespace

bool IsValid(const SessionTabSnapshot &tab) noexcept {
  return IsAllowedUrl(tab.url) &&
         (!tab.group || (!tab.group->empty() &&
                         tab.group->size() <= kMaxSessionGroupBytes &&
                         !HasControl(*tab.group)));
}

bool IsValid(const SessionWindowSnapshot &window) noexcept {
  if (!SessionRestoreCoordinator::IsValidId(window.window_id) ||
      window.tabs.empty() || window.tabs.size() > kMaxTabsPerWindow ||
      window.active_index >= window.tabs.size()) {
    return false;
  }
  std::unordered_set<std::string> groups;
  for (const auto &tab : window.tabs) {
    if (!IsValid(tab)) {
      return false;
    }
    if (tab.group && groups.insert(*tab.group).second &&
        groups.size() > kMaxSessionGroupsPerWindow) {
      return false;
    }
  }
  return true;
}

bool IsValid(const SessionProfileSnapshot &profile) noexcept {
  if (!SessionRestoreCoordinator::IsValidId(profile.profile_id) ||
      profile.windows.empty() ||
      profile.windows.size() > kMaxWindowsPerProfile) {
    return false;
  }
  std::unordered_set<std::string> ids;
  for (const auto &window : profile.windows) {
    if (!IsValid(window) || !ids.insert(window.window_id).second) {
      return false;
    }
  }
  return true;
}

std::optional<std::string>
EncodeSessionSnapshotV2(const SessionProfileSnapshot &profile,
                        SessionSnapshotError *error) {
  SetError(error, SessionSnapshotError::kNone);
  if (!IsValid(profile)) {
    SetError(error, SessionSnapshotError::kInvalidValue);
    return std::nullopt;
  }
  std::ostringstream out;
  out << kV2Header << '\n' << "P\t" << HexEncode(profile.profile_id) << '\n';
  for (const auto &window : profile.windows) {
    out << "W\t" << HexEncode(window.window_id) << '\t'
        << window.active_index << '\t' << window.tabs.size() << '\n';
    for (const auto &tab : window.tabs) {
      out << "T\t" << HexEncode(tab.url) << '\t' << (tab.pinned ? 1 : 0)
          << '\t' << (tab.muted ? 1 : 0) << '\t'
          << HexEncode(tab.group.value_or(std::string{})) << '\n';
    }
  }
  std::string encoded = out.str();
  if (encoded.size() > kMaxSessionBytes) {
    SetError(error, SessionSnapshotError::kTooLarge);
    return std::nullopt;
  }
  return encoded;
}

std::optional<SessionProfileSnapshot>
DecodeSessionSnapshot(const std::string &encoded, SessionSnapshotError *error) {
  SetError(error, SessionSnapshotError::kNone);
  if (encoded.empty() || encoded.size() > kMaxSessionBytes) {
    SetError(error, encoded.size() > kMaxSessionBytes
                        ? SessionSnapshotError::kTooLarge
                        : SessionSnapshotError::kMalformed);
    return std::nullopt;
  }
  const auto lines = Lines(encoded);
  if (lines.size() < 2 ||
      (lines[0] != kV1Header && lines[0] != kV2Header)) {
    SetError(error, lines.empty() ? SessionSnapshotError::kMalformed
                                  : SessionSnapshotError::kUnsupportedVersion);
    return std::nullopt;
  }
  const bool v1 = lines[0] == kV1Header;
  const auto profile_fields = Fields(lines[1]);
  const auto profile_id =
      profile_fields.size() == 2 && profile_fields[0] == "P"
          ? HexDecode(profile_fields[1])
          : std::nullopt;
  if (!profile_id) {
    SetError(error, SessionSnapshotError::kMalformed);
    return std::nullopt;
  }
  SessionProfileSnapshot profile{*profile_id, {}};
  std::unordered_set<std::string> ids;
  std::size_t line_index = 2;
  while (line_index < lines.size()) {
    if (lines[line_index].empty()) {
      ++line_index;
      continue;
    }
    const auto window_fields = Fields(lines[line_index++]);
    const std::size_t expected_fields = v1 ? 3 : 4;
    if (window_fields.size() != expected_fields ||
        window_fields[0] != "W") {
      SetError(error, SessionSnapshotError::kMalformed);
      return std::nullopt;
    }
    const auto window_id = HexDecode(window_fields[1]);
    const auto first_number = ParseSize(window_fields[2]);
    const auto tab_count =
        v1 ? first_number : ParseSize(window_fields[3]);
    const std::size_t active_index = v1 ? 0 : first_number.value_or(0);
    if (!window_id || !first_number || !tab_count || *tab_count == 0) {
      SetError(error, SessionSnapshotError::kInvalidValue);
      return std::nullopt;
    }
    if (*tab_count > kMaxTabsPerWindow ||
        profile.windows.size() >= kMaxWindowsPerProfile) {
      SetError(error, SessionSnapshotError::kCapacityExceeded);
      return std::nullopt;
    }
    if (!ids.insert(*window_id).second) {
      SetError(error, SessionSnapshotError::kDuplicateIdentity);
      return std::nullopt;
    }
    SessionWindowSnapshot window{*window_id, {}, active_index};
    if (v1) {
      window.tabs.assign(*tab_count, SessionTabSnapshot{std::string(kFallbackUrl)});
    } else {
      for (std::size_t tab = 0; tab < *tab_count; ++tab) {
        if (line_index >= lines.size()) {
          SetError(error, SessionSnapshotError::kMalformed);
          return std::nullopt;
        }
        const auto fields = Fields(lines[line_index++]);
        const auto url = fields.size() == 5 && fields[0] == "T"
                             ? HexDecode(fields[1])
                             : std::nullopt;
        const auto group = fields.size() == 5 ? HexDecode(fields[4])
                                               : std::nullopt;
        if (!url || !group || (fields[2] != "0" && fields[2] != "1") ||
            (fields[3] != "0" && fields[3] != "1")) {
          SetError(error, SessionSnapshotError::kMalformed);
          return std::nullopt;
        }
        window.tabs.push_back({*url, fields[2] == "1", fields[3] == "1",
                               group->empty()
                                   ? std::nullopt
                                   : std::optional<std::string>(*group)});
      }
    }
    profile.windows.push_back(std::move(window));
  }
  if (!IsValid(profile)) {
    SetError(error, SessionSnapshotError::kInvalidValue);
    return std::nullopt;
  }
  return profile;
}

} // namespace crayon::browser_session
