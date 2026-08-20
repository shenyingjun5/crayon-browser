#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "crayon/browser_omnibox_provider/search_provider.h"

namespace {

using crayon::browser_omnibox_provider::DeriveSearchRequestPolicy;
using crayon::browser_omnibox_provider::kMaxProviders;
using crayon::browser_omnibox_provider::ProviderError;
using crayon::browser_omnibox_provider::ResolveSchemelessUrl;
using crayon::browser_omnibox_provider::SearchProvider;
using crayon::browser_omnibox_provider::SearchProviderSet;
using crayon::browser_omnibox_provider::ValidateProvider;
using crayon::browser_privacy::CookiePolicy;
using crayon::browser_privacy::DefaultPrivacyDefaults;
using crayon::browser_privacy::PrivacyDefaults;
using crayon::browser_privacy::ReferrerPolicy;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

SearchProvider MakeProvider(const std::string& name,
                            const std::string& url_template) {
  return SearchProvider{name, url_template};
}

// ---------- Validation matrix ----------

bool ValidProviderAccepted() {
  const auto provider =
      MakeProvider("Example", "https://example.test/search?q={searchTerms}");
  CHECK(!ValidateProvider(provider).has_value());
  return true;
}

bool ValidationFailuresMatrix() {
  CHECK(ValidateProvider(MakeProvider(
            "", "https://example.test/?q={searchTerms}")) ==
        std::optional<ProviderError>(ProviderError::kEmptyName));
  CHECK(ValidateProvider(MakeProvider(
            "X", std::string(2050, 'a') + "{searchTerms}")) ==
        std::optional<ProviderError>(ProviderError::kTemplateTooLong));
  CHECK(ValidateProvider(MakeProvider(
            "X", "ftp://example.test/?q={searchTerms}")) ==
        std::optional<ProviderError>(ProviderError::kTemplateNotHttp));
  CHECK(ValidateProvider(MakeProvider(
            "X", "javascript:q={searchTerms}")) ==
        std::optional<ProviderError>(ProviderError::kTemplateNotHttp));
  CHECK(ValidateProvider(MakeProvider(
            "X", "https://user:pass@example.test/?q={searchTerms}")) ==
        std::optional<ProviderError>(ProviderError::kTemplateHasCredentials));
  CHECK(ValidateProvider(MakeProvider(
            "X", std::string("https://example.test/\x01?q={searchTerms}"))) ==
        std::optional<ProviderError>(ProviderError::kTemplateHasControlChars));
  CHECK(ValidateProvider(MakeProvider("X", "https://example.test/search")) ==
        std::optional<ProviderError>(ProviderError::kPlaceholderMissing));
  CHECK(ValidateProvider(MakeProvider(
            "X",
            "https://example.test/?q={searchTerms}&r={searchTerms}")) ==
        std::optional<ProviderError>(ProviderError::kPlaceholderRepeated));
  return true;
}

// ---------- Provider set ----------

bool EmptySetYieldsNoUrl() {
  // No default public search engine is built in.
  const SearchProviderSet set;
  CHECK(set.Primary() == nullptr);
  CHECK(!set.BuildSearchUrl("hello").has_value());
  return true;
}

bool CapacityEnforcedAndInvalidRejected() {
  SearchProviderSet set;
  for (std::size_t i = 0; i < kMaxProviders; ++i) {
    CHECK(set.Add(MakeProvider(
        "p" + std::to_string(i),
        "https://example.test/" + std::to_string(i) + "?q={searchTerms}")));
  }
  CHECK(!set.Add(MakeProvider("overflow",
                              "https://example.test/?q={searchTerms}")));
  CHECK(!set.Add(MakeProvider("bad", "ftp://x/?q={searchTerms}")));
  CHECK(set.size() == kMaxProviders);
  return true;
}

bool BuildSearchUrlEncodesTerms() {
  SearchProviderSet set;
  set.Add(MakeProvider("Example", "https://example.test/s?q={searchTerms}"));
  const auto url = set.BuildSearchUrl("蜡笔 browser&more");
  CHECK(url.has_value());
  CHECK(*url == "https://example.test/s?q=%E8%9C%A1%E7%AC%94%20browser%26more");
  return true;
}

bool BuildSearchUrlRespectsPriority() {
  SearchProviderSet set;
  set.Add(MakeProvider("First", "https://first.test/?q={searchTerms}"));
  set.Add(MakeProvider("Second", "https://second.test/?q={searchTerms}"));
  const auto url = set.BuildSearchUrl("x");
  CHECK(url.has_value() && url->find("first.test") != std::string::npos);
  return true;
}

// ---------- HTTPS default upgrade ----------

bool HttpsDefaultUpgradeMatrix() {
  PrivacyDefaults privacy = DefaultPrivacyDefaults();
  CHECK(privacy.https_default);
  CHECK(ResolveSchemelessUrl("example.com", privacy) ==
        "https://example.com");
  privacy.https_default = false;
  CHECK(ResolveSchemelessUrl("example.com", privacy) ==
        "http://example.com");
  return true;
}

// ---------- Privacy injection ----------

bool SearchRequestPolicyFollowsPrivacy() {
  PrivacyDefaults privacy = DefaultPrivacyDefaults();
  auto policy = DeriveSearchRequestPolicy(privacy);
  CHECK(policy.referrer_policy ==
        ReferrerPolicy::kStrictOriginWhenCrossOrigin);
  CHECK(!policy.send_third_party_cookies);  // block-third-party default

  privacy.cookie_policy = CookiePolicy::kAllow;
  privacy.referrer_policy = ReferrerPolicy::kNoReferrer;
  policy = DeriveSearchRequestPolicy(privacy);
  CHECK(policy.referrer_policy == ReferrerPolicy::kNoReferrer);
  CHECK(policy.send_third_party_cookies);

  privacy.cookie_policy = CookiePolicy::kBlockAll;
  policy = DeriveSearchRequestPolicy(privacy);
  CHECK(!policy.send_third_party_cookies);
  return true;
}

}  // namespace

int main() {
  if (!ValidProviderAccepted() || !ValidationFailuresMatrix() ||
      !EmptySetYieldsNoUrl() || !CapacityEnforcedAndInvalidRejected() ||
      !BuildSearchUrlEncodesTerms() || !BuildSearchUrlRespectsPriority() ||
      !HttpsDefaultUpgradeMatrix() || !SearchRequestPolicyFollowsPrivacy()) {
    return 1;
  }
  return 0;
}
