#include "crayon/browser_downloads/download_path.h"

#include <utility>

namespace crayon::browser_downloads {

namespace {

bool IsPathSeparator(char c) noexcept {
  return c == '/' || c == '\\';
}

bool IsAsciiControl(char c) noexcept {
  const unsigned char uc = static_cast<unsigned char>(c);
  return uc < 0x20 || uc == 0x7F;
}

/// Splits `file_name` into stem and final extension (dot included).  Names
/// without a usable extension return the whole name as the stem.
std::pair<std::string, std::string> SplitExtension(
    const std::string& file_name) {
  const std::size_t dot = file_name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) {
    return {file_name, std::string{}};
  }
  return {file_name.substr(0, dot), file_name.substr(dot)};
}

}  // namespace

std::optional<std::string> SanitizeDownloadFileName(
    const std::string& untrusted_name) {
  std::string clean;
  clean.reserve(untrusted_name.size());
  for (const char c : untrusted_name) {
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
  if (clean.empty() || first_non_dot == std::string::npos) {
    return std::nullopt;
  }
  return clean;
}

std::optional<std::string> ResolveUniqueDownloadPath(
    const std::string& directory,
    const std::string& file_name,
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

}  // namespace crayon::browser_downloads
