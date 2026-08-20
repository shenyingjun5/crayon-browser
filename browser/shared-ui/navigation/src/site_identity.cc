#include "crayon/browser_navigation/site_identity.h"

#include <string_view>

namespace crayon::browser_navigation {

namespace {

bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

SiteIdentity EvaluateSiteIdentity(const std::string& url) noexcept {
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

}  // namespace crayon::browser_navigation
