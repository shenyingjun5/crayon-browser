#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_privacy/privacy_defaults.h"

namespace {

using crayon::browser_privacy::CookiePolicy;
using crayon::browser_privacy::DefaultPrivacyDefaults;
using crayon::browser_privacy::Describe;
using crayon::browser_privacy::IsValid;
using crayon::browser_privacy::Normalize;
using crayon::browser_privacy::PermissionDefault;
using crayon::browser_privacy::PrivacyDefaults;
using crayon::browser_privacy::ReferrerPolicy;
using crayon::browser_privacy::Validate;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool DefaultsAreMostConservative() {
  const PrivacyDefaults defaults = DefaultPrivacyDefaults();
  CHECK(defaults.cookie_policy == CookiePolicy::kBlockThirdParty);
  CHECK(defaults.referrer_policy ==
        ReferrerPolicy::kStrictOriginWhenCrossOrigin);
  CHECK(defaults.permission_default == PermissionDefault::kDeny);
  CHECK(defaults.storage_partitioning);
  CHECK(defaults.https_default);
  CHECK(Validate(defaults));
  return true;
}

bool EveryLegalCombinationValidates() {
  const CookiePolicy cookies[] = {CookiePolicy::kAllow,
                                  CookiePolicy::kBlockThirdParty,
                                  CookiePolicy::kBlockAll};
  const ReferrerPolicy referrers[] = {
      ReferrerPolicy::kStrictOriginWhenCrossOrigin,
      ReferrerPolicy::kStrictOrigin, ReferrerPolicy::kNoReferrer};
  const PermissionDefault permissions[] = {PermissionDefault::kDeny,
                                           PermissionDefault::kAskOnce};
  for (const CookiePolicy cookie : cookies) {
    for (const ReferrerPolicy referrer : referrers) {
      for (const PermissionDefault permission : permissions) {
        for (const bool partitioning : {false, true}) {
          for (const bool https_default : {false, true}) {
            PrivacyDefaults candidate;
            candidate.cookie_policy = cookie;
            candidate.referrer_policy = referrer;
            candidate.permission_default = permission;
            candidate.storage_partitioning = partitioning;
            candidate.https_default = https_default;
            CHECK(Validate(candidate));
          }
        }
      }
    }
  }
  return true;
}

bool OutOfDomainEnumsFailClosed() {
  PrivacyDefaults candidate;
  candidate.cookie_policy = static_cast<CookiePolicy>(99);
  CHECK(!Validate(candidate));

  candidate = PrivacyDefaults{};
  candidate.referrer_policy = static_cast<ReferrerPolicy>(7);
  CHECK(!Validate(candidate));

  candidate = PrivacyDefaults{};
  candidate.permission_default = static_cast<PermissionDefault>(3);
  CHECK(!Validate(candidate));
  return true;
}

bool NormalizeFallsBackToDefaultsOnInvalid() {
  PrivacyDefaults candidate;
  candidate.cookie_policy = CookiePolicy::kAllow;
  candidate.referrer_policy = static_cast<ReferrerPolicy>(-1);
  candidate.storage_partitioning = false;
  candidate.https_default = false;
  const PrivacyDefaults normalized = Normalize(candidate);
  CHECK(normalized.cookie_policy == CookiePolicy::kBlockThirdParty);
  CHECK(normalized.storage_partitioning);
  CHECK(normalized.https_default);
  return true;
}

bool NormalizeKeepsValidCandidates() {
  PrivacyDefaults candidate;
  candidate.cookie_policy = CookiePolicy::kAllow;
  candidate.referrer_policy = ReferrerPolicy::kNoReferrer;
  candidate.permission_default = PermissionDefault::kAskOnce;
  candidate.storage_partitioning = false;
  candidate.https_default = false;
  const PrivacyDefaults normalized = Normalize(candidate);
  CHECK(normalized.cookie_policy == CookiePolicy::kAllow);
  CHECK(normalized.referrer_policy == ReferrerPolicy::kNoReferrer);
  CHECK(normalized.permission_default == PermissionDefault::kAskOnce);
  CHECK(!normalized.storage_partitioning);
  CHECK(!normalized.https_default);
  return true;
}

bool EnumClosureIsComplete() {
  CHECK(IsValid(CookiePolicy::kAllow));
  CHECK(IsValid(CookiePolicy::kBlockThirdParty));
  CHECK(IsValid(CookiePolicy::kBlockAll));
  CHECK(!IsValid(static_cast<CookiePolicy>(9)));
  CHECK(IsValid(ReferrerPolicy::kNoReferrer));
  CHECK(!IsValid(static_cast<ReferrerPolicy>(9)));
  CHECK(IsValid(PermissionDefault::kAskOnce));
  CHECK(!IsValid(static_cast<PermissionDefault>(9)));
  return true;
}

bool DescribeMatchesGoldenSnapshot() {
  // Compatibility golden: the default snapshot must stay byte-identical.
  const std::string golden =
      "cookie=block-third-party;referrer=strict-origin-when-cross-origin;"
      "permission=deny;storage-partitioning=on;https-default=on";
  CHECK(Describe(DefaultPrivacyDefaults()) == golden);

  PrivacyDefaults custom;
  custom.cookie_policy = CookiePolicy::kBlockAll;
  custom.referrer_policy = ReferrerPolicy::kNoReferrer;
  custom.permission_default = PermissionDefault::kAskOnce;
  custom.storage_partitioning = false;
  custom.https_default = false;
  CHECK(Describe(custom) ==
        "cookie=block-all;referrer=no-referrer;permission=ask-once;"
        "storage-partitioning=off;https-default=off");
  return true;
}

}  // namespace

int main() {
  if (!DefaultsAreMostConservative() || !EveryLegalCombinationValidates() ||
      !OutOfDomainEnumsFailClosed() || !NormalizeFallsBackToDefaultsOnInvalid() ||
      !NormalizeKeepsValidCandidates() || !EnumClosureIsComplete() ||
      !DescribeMatchesGoldenSnapshot()) {
    return 1;
  }
  return 0;
}
