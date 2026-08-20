#pragma once

#include <string>

namespace crayon::browser_navigation {

/// Security identity of the current page, derived from its URL scheme.
/// Certificate-level details are out of scope; they are provided by CEF-05.
enum class SiteIdentity {
  kUnknown = 0,
  kSecure,      // https://
  kInsecure,    // http://
  kLocal,       // file://, crayon://, about:
  kDangerous,   // javascript:, data:, vbscript:
};

constexpr bool IsValid(SiteIdentity identity) noexcept {
  switch (identity) {
    case SiteIdentity::kUnknown:
    case SiteIdentity::kSecure:
    case SiteIdentity::kInsecure:
    case SiteIdentity::kLocal:
    case SiteIdentity::kDangerous:
      return true;
  }
  return false;
}

/// Derives site identity from a raw URL string without network access.
/// Empty or unrecognised input returns kUnknown.
SiteIdentity EvaluateSiteIdentity(const std::string& url) noexcept;

}  // namespace crayon::browser_navigation
