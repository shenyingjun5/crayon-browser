#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace crayon::browser_omnibox {

/// Maximum raw input length for the omnibox (same bound as BrowserUrl).
inline constexpr std::size_t kMaxOmniboxInputBytes = 2048;

/// Result of parsing raw user input typed into the omnibox.
enum class OmniboxParseResult {
  /// A navigable URL with an allowed scheme or recognised authority shape.
  kValidUrl = 0,
  /// Input that is not a URL; should be forwarded to the search provider.
  kSearchQuery,
  /// Input carries a dangerous scheme (javascript:, data:, vbscript:).
  /// Navigation must be blocked; display is a search query.
  kDangerous,
};

constexpr bool IsValid(OmniboxParseResult result) noexcept {
  switch (result) {
    case OmniboxParseResult::kValidUrl:
    case OmniboxParseResult::kSearchQuery:
    case OmniboxParseResult::kDangerous:
      return true;
  }
  return false;
}

/// Normalised user input with length validation.
/// Empty input is accepted at the boundary but parses to kSearchQuery.
class OmniboxInput final {
 public:
  /// Creates an input if length is within the allowed bound.
  static std::optional<OmniboxInput> TryCreate(std::string value);

  const std::string& value() const noexcept { return value_; }
  bool empty() const noexcept { return value_.empty(); }
  std::size_t length() const noexcept { return value_.length(); }

  friend bool operator==(const OmniboxInput& left,
                         const OmniboxInput& right) noexcept {
    return left.value_ == right.value_;
  }

 private:
  explicit OmniboxInput(std::string value) : value_(std::move(value)) {}
  std::string value_;
};

/// Categorises raw user input without network access.
///
/// Rules (in order):
///  1. Empty        -> kSearchQuery
///  2. Dangerous    -> kDangerous   (javascript:, data:, vbscript:)
///  3. Known scheme -> kValidUrl    (http, https, file, crayon)
///  4. IPv4 literal -> kValidUrl
///  5. Domain-like  -> kValidUrl    (contains '.' with alphabetic suffix)
///  6. Everything else -> kSearchQuery
OmniboxParseResult ParseOmniboxInput(const OmniboxInput& input) noexcept;

}  // namespace crayon::browser_omnibox
