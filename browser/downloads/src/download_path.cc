#include "crayon/browser_downloads/download_path.h"

#include <string_view>
#include <utility>

namespace crayon::browser_downloads {

namespace {

bool IsPathSeparator(char c) noexcept { return c == '/' || c == '\\'; }

bool IsAsciiControl(char c) noexcept {
  const unsigned char uc = static_cast<unsigned char>(c);
  return uc < 0x20 || uc == 0x7F;
}

bool IsPortableInvalidFileNameChar(char c) noexcept {
  switch (c) {
  case '<':
  case '>':
  case ':':
  case '"':
  case '|':
  case '?':
  case '*':
    return true;
  default:
    return false;
  }
}

bool EqualsIgnoreAsciiCase(std::string_view lhs,
                           std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size())
    return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (static_cast<char>(std::tolower(static_cast<unsigned char>(lhs[i]))) !=
        static_cast<char>(std::tolower(static_cast<unsigned char>(rhs[i])))) {
      return false;
    }
  }
  return true;
}

bool IsWindowsReservedDeviceName(const std::string &file_name) noexcept {
  const std::string_view name(file_name);
  const std::size_t dot = name.find('.');
  const std::string_view stem = name.substr(0, dot);
  if (EqualsIgnoreAsciiCase(stem, "CON") ||
      EqualsIgnoreAsciiCase(stem, "PRN") ||
      EqualsIgnoreAsciiCase(stem, "AUX") ||
      EqualsIgnoreAsciiCase(stem, "NUL")) {
    return true;
  }
  if (stem.size() != 4)
    return false;
  const char suffix = stem[3];
  return suffix >= '1' && suffix <= '9' &&
         (EqualsIgnoreAsciiCase(stem.substr(0, 3), "COM") ||
          EqualsIgnoreAsciiCase(stem.substr(0, 3), "LPT"));
}

/// Splits `file_name` into stem and final extension (dot included).  Names
/// without a usable extension return the whole name as the stem.
std::pair<std::string, std::string>
SplitExtension(const std::string &file_name) {
  const std::size_t dot = file_name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) {
    return {file_name, std::string{}};
  }
  return {file_name.substr(0, dot), file_name.substr(dot)};
}

} // namespace

std::optional<std::string>
SanitizeDownloadFileName(const std::string &untrusted_name) {
  std::string clean;
  clean.reserve(untrusted_name.size());
  for (const char c : untrusted_name) {
    if (IsPortableInvalidFileNameChar(c))
      return std::nullopt;
    if (IsPathSeparator(c) || IsAsciiControl(c)) {
      continue;
    }
    clean.push_back(c);
    if (clean.size() > kMaxFileNameLength) {
      return std::nullopt;
    }
  }
  // Trailing dots and spaces are illegal on Windows and confusing anywhere.
  while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) {
    clean.pop_back();
  }
  // Leading dots alone (e.g. "..", ".") must not survive either.
  const std::size_t first_non_dot = clean.find_first_not_of('.');
  if (clean.empty() || first_non_dot == std::string::npos ||
      IsWindowsReservedDeviceName(clean)) {
    return std::nullopt;
  }
  return clean;
}

std::optional<std::string>
ResolveUniqueDownloadPath(const std::string &directory,
                          const std::string &file_name,
                          PathExistsPredicate path_exists) {
  if (directory.empty() || file_name.empty() || path_exists == nullptr) {
    return std::nullopt;
  }
  const auto [stem, extension] = SplitExtension(file_name);
  for (unsigned n = 0; n <= kMaxDedupeIndex; ++n) {
    std::string candidate_name = file_name;
    if (n != 0) {
      candidate_name = stem + " (" + std::to_string(n) + ")" + extension;
      if (candidate_name.size() > kMaxFileNameLength) {
        return std::nullopt;
      }
    }
    std::string candidate = directory;
    if (!IsPathSeparator(candidate.back())) {
      candidate.push_back('/');
    }
    candidate += candidate_name;
    if (!path_exists(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

} // namespace crayon::browser_downloads
