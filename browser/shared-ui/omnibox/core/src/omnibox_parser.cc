#include "crayon/browser_omnibox/omnibox_parser.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace crayon::browser_omnibox {

namespace {

constexpr std::string_view kDangerousSchemes[] = {
    "javascript:", "data:", "vbscript:",
};

constexpr std::string_view kAllowedSchemes[] = {
    "http://", "https://", "file:", "crayon://",
};

// Simplified IPv4 detection: four decimal octets separated by dots.
// Rejects leading zeros and out-of-range octets.
bool IsIpv4Literal(std::string_view text) noexcept {
  if (text.empty()) return false;

  std::size_t octets = 0;
  std::size_t pos = 0;
  while (pos < text.size() && octets < 4) {
    std::size_t dot = text.find('.', pos);
    std::string_view octet =
        (dot == std::string_view::npos) ? text.substr(pos) : text.substr(pos, dot - pos);

    if (octet.empty() || octet.size() > 3) return false;
    if (octet.size() > 1 && octet[0] == '0') return false;  // leading zero
    for (char c : octet) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    int value = 0;
    for (char c : octet) {
      value = value * 10 + (c - '0');
    }
    if (value > 255) return false;

    ++octets;
    if (dot == std::string_view::npos) {
      pos = text.size();
    } else {
      pos = dot + 1;
    }
  }
  return octets == 4 && pos >= text.size();
}

// True if the input looks like a domain: contains at least one '.',
// the last label is alphabetic (2..63 chars), and there are no spaces.
bool IsDomainLike(std::string_view text) noexcept {
  if (text.empty()) return false;
  if (text.find(' ') != std::string_view::npos) return false;

  std::size_t last_dot = text.rfind('.');
  if (last_dot == std::string_view::npos || last_dot + 1 >= text.size()) {
    return false;
  }

  std::string_view tld = text.substr(last_dot + 1);
  if (tld.size() < 2 || tld.size() > 63) return false;

  for (char c : tld) {
    if (!std::isalpha(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

std::optional<OmniboxInput> OmniboxInput::TryCreate(std::string value) {
  if (value.length() > kMaxOmniboxInputBytes) {
    return std::nullopt;
  }
  return OmniboxInput(std::move(value));
}

OmniboxParseResult ParseOmniboxInput(const OmniboxInput& input) noexcept {
  if (input.empty()) {
    return OmniboxParseResult::kSearchQuery;
  }

  const std::string_view text = input.value();

  // 1. Dangerous schemes
  for (std::string_view scheme : kDangerousSchemes) {
    if (StartsWith(text, scheme)) {
      return OmniboxParseResult::kDangerous;
    }
  }

  // 2. Allowed schemes
  for (std::string_view scheme : kAllowedSchemes) {
    if (StartsWith(text, scheme)) {
      return OmniboxParseResult::kValidUrl;
    }
  }

  // 3. Extract the host-or-path portion for further tests.
  //    Anything before the first '/' is the authority; after that is path.
  std::string_view authority = text;
  std::size_t slash = text.find('/');
  if (slash != std::string_view::npos) {
    authority = text.substr(0, slash);
  }

  // 4. IPv4 literal
  if (IsIpv4Literal(authority)) {
    return OmniboxParseResult::kValidUrl;
  }

  // 5. Domain-like
  if (IsDomainLike(authority)) {
    return OmniboxParseResult::kValidUrl;
  }

  // 6. Search query
  return OmniboxParseResult::kSearchQuery;
}

}  // namespace crayon::browser_omnibox
