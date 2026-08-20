#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_privacy/privacy_defaults.h"

namespace crayon::browser_omnibox_provider {

/// Maximum length of a provider search URL template in bytes.
inline constexpr std::size_t kMaxTemplateBytes = 2048;

/// Maximum number of configured search providers.
inline constexpr std::size_t kMaxProviders = 8;

/// The single placeholder a search URL template must contain exactly once.
inline constexpr char kSearchTermsPlaceholder[] = "{searchTerms}";

/// One user-configured search provider (schema version 1).
///
/// Providers are compile-time/locally configured memory structures; there is
/// no default public search engine — an empty provider set means omnibox
/// search input cannot be submitted to any remote service.
struct SearchProvider final {
  std::string name;
  /// e.g. "https://example.test/search?q={searchTerms}"
  std::string url_template;
};

/// Validation failure for a provider entry.
enum class ProviderError {
  kEmptyName = 0,
  kTemplateTooLong,
  kTemplateNotHttp,
  kTemplateHasCredentials,
  kTemplateHasControlChars,
  kPlaceholderMissing,
  kPlaceholderRepeated,
};

/// Validates one provider entry.  Returns the failure reason, or
/// `std::nullopt` when the entry is valid; nothing is repaired.
std::optional<ProviderError> ValidateProvider(
    const SearchProvider& provider) noexcept;

/// Ordered, bounded set of validated providers.
class SearchProviderSet final {
 public:
  /// Adds a provider after validation; returns false on validation failure
  /// or capacity overflow.
  bool Add(SearchProvider provider) noexcept;

  /// Highest-priority provider, if any.
  const SearchProvider* Primary() const noexcept;

  std::size_t size() const noexcept { return providers_.size(); }

  /// Builds the final search URL for `terms` using the primary provider.
  /// Search terms are percent-encoded (UTF-8, unreserved characters kept).
  /// Returns nullopt when no provider is configured or the result would
  /// exceed the template length bound.
  std::optional<std::string> BuildSearchUrl(const std::string& terms) const;

 private:
  std::vector<SearchProvider> providers_;  // Ordered by priority.
};

/// Decides the scheme for omnibox input the parser classified as a URL
/// without a scheme (e.g. "example.com").
///
/// When `PrivacyDefaults::https_default` is on, "https://" is prepended;
/// otherwise "http://".  No downgrade happens after a failed upgrade — the
/// navigation layer reports connection failures.
std::string ResolveSchemelessUrl(const std::string& host_and_path,
                                 const crayon::browser_privacy::PrivacyDefaults&
                                     privacy) noexcept;

/// Privacy parameters attached to an outgoing search request.
struct SearchRequestPolicy final {
  crayon::browser_privacy::ReferrerPolicy referrer_policy;
  /// Third-party cookies are sent only when the cookie policy fully allows
  /// them; privacy always wins over provider preference.
  bool send_third_party_cookies;
};

/// Derives the search request policy from the privacy defaults.
SearchRequestPolicy DeriveSearchRequestPolicy(
    const crayon::browser_privacy::PrivacyDefaults& privacy) noexcept;

}  // namespace crayon::browser_omnibox_provider
