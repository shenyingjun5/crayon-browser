#pragma once

#include <string>

namespace crayon::browser_navigation {

/// Security identity of the current page, derived from its URL scheme.
/// Certificate-level details are out of scope; they are provided by CEF-05.
enum class SiteIdentity {
  kUnknown = 0,
  kSecure,           // successfully loaded https://
  kSecurePending,    // https:// before successful completion
  kCertificateError, // https:// with a certificate/SSL failure
  kInsecure,         // http://
  kLocal,            // file://, crayon://, about:
  kDangerous,        // javascript:, data:, vbscript:
};

constexpr bool IsValid(SiteIdentity identity) noexcept {
  switch (identity) {
  case SiteIdentity::kUnknown:
  case SiteIdentity::kSecure:
  case SiteIdentity::kSecurePending:
  case SiteIdentity::kCertificateError:
  case SiteIdentity::kInsecure:
  case SiteIdentity::kLocal:
  case SiteIdentity::kDangerous:
    return true;
  }
  return false;
}

/// Derives site identity from a raw URL string without network access.
/// Empty or unrecognised input returns kUnknown.
SiteIdentity EvaluateSiteIdentity(const std::string &url) noexcept;

/// Derives the identity presented by browser chrome from trusted navigation
/// evidence. HTTPS is not reported as secure until the current main-frame
/// navigation has completed successfully; a certificate error always wins.
SiteIdentity EvaluateSiteIdentity(const std::string &url,
                                  bool navigation_succeeded,
                                  bool certificate_error) noexcept;

} // namespace crayon::browser_navigation
