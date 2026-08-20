#include "crayon/browser_omnibox_provider/search_provider.h"

#include <string_view>

namespace crayon::browser_omnibox_provider {

namespace {

using crayon::browser_privacy::CookiePolicy;
using crayon::browser_privacy::PrivacyDefaults;

bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

std::size_t CountOccurrences(std::string_view text,
                             std::string_view needle) noexcept {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string_view::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

bool HasControlChars(std::string_view text) noexcept {
  for (const char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7F) {
      return true;
    }
  }
  return false;
}

/// Percent-encodes UTF-8 text keeping RFC 3986 unreserved characters.
std::string PercentEncode(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (const unsigned char c : text) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                            c == '.' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

}  // namespace

std::optional<ProviderError> ValidateProvider(
    const SearchProvider& provider) noexcept {
  if (provider.name.empty()) {
    return ProviderError::kEmptyName;
  }
  const std::string_view url = provider.url_template;
  if (url.size() > kMaxTemplateBytes) {
    return ProviderError::kTemplateTooLong;
  }
  if (!StartsWith(url, "https://") && !StartsWith(url, "http://")) {
    return ProviderError::kTemplateNotHttp;
  }
  const std::size_t authority_begin = url.find("://") + 3;
  const std::size_t authority_end = url.find('/', authority_begin);
  const std::string_view authority =
      authority_end == std::string_view::npos
          ? url.substr(authority_begin)
          : url.substr(authority_begin, authority_end - authority_begin);
  if (authority.find('@') != std::string_view::npos) {
    return ProviderError::kTemplateHasCredentials;
  }
  if (HasControlChars(url)) {
    return ProviderError::kTemplateHasControlChars;
  }
  const std::size_t placeholders =
      CountOccurrences(url, kSearchTermsPlaceholder);
  if (placeholders == 0) {
    return ProviderError::kPlaceholderMissing;
  }
  if (placeholders > 1) {
    return ProviderError::kPlaceholderRepeated;
  }
  return std::nullopt;
}

bool SearchProviderSet::Add(SearchProvider provider) noexcept {
  if (providers_.size() >= kMaxProviders) {
    return false;
  }
  if (ValidateProvider(provider).has_value()) {
    return false;
  }
  providers_.push_back(std::move(provider));
  return true;
}

const SearchProvider* SearchProviderSet::Primary() const noexcept {
  return providers_.empty() ? nullptr : &providers_.front();
}

std::optional<std::string> SearchProviderSet::BuildSearchUrl(
    const std::string& terms) const {
  const SearchProvider* provider = Primary();
  if (provider == nullptr) {
    return std::nullopt;
  }
  const std::string& url_template = provider->url_template;
  const std::size_t pos = url_template.find(kSearchTermsPlaceholder);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  std::string url = url_template.substr(0, pos);
  url += PercentEncode(terms);
  url += url_template.substr(pos + std::string_view(kSearchTermsPlaceholder).size());
  if (url.size() > kMaxTemplateBytes) {
    return std::nullopt;
  }
  return url;
}

std::string ResolveSchemelessUrl(const std::string& host_and_path,
                                 const PrivacyDefaults& privacy) noexcept {
  return (privacy.https_default ? "https://" : "http://") + host_and_path;
}

SearchRequestPolicy DeriveSearchRequestPolicy(
    const PrivacyDefaults& privacy) noexcept {
  SearchRequestPolicy policy;
  policy.referrer_policy = privacy.referrer_policy;
  policy.send_third_party_cookies =
      privacy.cookie_policy == CookiePolicy::kAllow;
  return policy;
}

}  // namespace crayon::browser_omnibox_provider
