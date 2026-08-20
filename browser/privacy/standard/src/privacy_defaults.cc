#include "crayon/browser_privacy/privacy_defaults.h"

namespace crayon::browser_privacy {

namespace {

const char* CookiePolicyName(CookiePolicy policy) noexcept {
  switch (policy) {
    case CookiePolicy::kAllow:
      return "allow";
    case CookiePolicy::kBlockThirdParty:
      return "block-third-party";
    case CookiePolicy::kBlockAll:
      return "block-all";
  }
  return "unknown";
}

const char* ReferrerPolicyName(ReferrerPolicy policy) noexcept {
  switch (policy) {
    case ReferrerPolicy::kStrictOriginWhenCrossOrigin:
      return "strict-origin-when-cross-origin";
    case ReferrerPolicy::kStrictOrigin:
      return "strict-origin";
    case ReferrerPolicy::kNoReferrer:
      return "no-referrer";
  }
  return "unknown";
}

const char* PermissionDefaultName(PermissionDefault policy) noexcept {
  switch (policy) {
    case PermissionDefault::kDeny:
      return "deny";
    case PermissionDefault::kAskOnce:
      return "ask-once";
  }
  return "unknown";
}

const char* BoolName(bool value) noexcept {
  return value ? "on" : "off";
}

}  // namespace

PrivacyDefaults DefaultPrivacyDefaults() noexcept {
  return PrivacyDefaults{};
}

bool Validate(const PrivacyDefaults& candidate) noexcept {
  return IsValid(candidate.cookie_policy) &&
         IsValid(candidate.referrer_policy) &&
         IsValid(candidate.permission_default);
}

PrivacyDefaults Normalize(const PrivacyDefaults& candidate) noexcept {
  if (!Validate(candidate)) {
    return DefaultPrivacyDefaults();
  }
  return candidate;
}

std::string Describe(const PrivacyDefaults& defaults) {
  std::string out;
  out.reserve(160);
  out += "cookie=";
  out += CookiePolicyName(defaults.cookie_policy);
  out += ";referrer=";
  out += ReferrerPolicyName(defaults.referrer_policy);
  out += ";permission=";
  out += PermissionDefaultName(defaults.permission_default);
  out += ";storage-partitioning=";
  out += BoolName(defaults.storage_partitioning);
  out += ";https-default=";
  out += BoolName(defaults.https_default);
  return out;
}

}  // namespace crayon::browser_privacy
