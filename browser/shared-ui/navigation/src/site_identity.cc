#include "crayon/browser_navigation/site_identity.h"

#include <string_view>

namespace crayon::browser_navigation {

namespace {

bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
  if (text.size() < prefix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    char character = text[index];
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
    if (character != prefix[index]) {
      return false;
    }
  }
  return true;
}

} // namespace

SiteIdentity EvaluateSiteIdentity(const std::string &url) noexcept {
  if (url.empty()) {
    return SiteIdentity::kUnknown;
  }

  if (StartsWith(url, "https://")) {
    return SiteIdentity::kSecure;
  }
  if (StartsWith(url, "http://")) {
    return SiteIdentity::kInsecure;
  }
  if (StartsWith(url, "file://") || StartsWith(url, "crayon://") ||
      StartsWith(url, "about:")) {
    return SiteIdentity::kLocal;
  }
  if (StartsWith(url, "javascript:") || StartsWith(url, "data:") ||
      StartsWith(url, "vbscript:")) {
    return SiteIdentity::kDangerous;
  }

  return SiteIdentity::kUnknown;
}

SiteIdentity EvaluateSiteIdentity(const std::string &url,
                                  bool navigation_succeeded,
                                  bool certificate_error) noexcept {
  const SiteIdentity scheme_identity = EvaluateSiteIdentity(url);
  if (scheme_identity != SiteIdentity::kSecure) {
    return scheme_identity;
  }
  if (certificate_error) {
    return SiteIdentity::kCertificateError;
  }
  return navigation_succeeded ? SiteIdentity::kSecure
                              : SiteIdentity::kSecurePending;
}

} // namespace crayon::browser_navigation
