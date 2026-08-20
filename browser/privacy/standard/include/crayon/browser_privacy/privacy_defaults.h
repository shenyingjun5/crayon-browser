#pragma once

#include <string>

namespace crayon::browser_privacy {

/// Third-party cookie policy.
enum class CookiePolicy {
  kAllow = 0,
  kBlockThirdParty,
  kBlockAll,
};

constexpr bool IsValid(CookiePolicy policy) noexcept {
  switch (policy) {
    case CookiePolicy::kAllow:
    case CookiePolicy::kBlockThirdParty:
    case CookiePolicy::kBlockAll:
      return true;
  }
  return false;
}

/// Referrer policy applied to outgoing requests.
enum class ReferrerPolicy {
  kStrictOriginWhenCrossOrigin = 0,
  kStrictOrigin,
  kNoReferrer,
};

constexpr bool IsValid(ReferrerPolicy policy) noexcept {
  switch (policy) {
    case ReferrerPolicy::kStrictOriginWhenCrossOrigin:
    case ReferrerPolicy::kStrictOrigin:
    case ReferrerPolicy::kNoReferrer:
      return true;
  }
  return false;
}

/// Default decision for a permission kind (camera, microphone, location,
/// notification, clipboard, downloads are gated the same way).
enum class PermissionDefault {
  kDeny = 0,
  /// Ask the user once; still denied until an explicit grant exists.
  kAskOnce,
};

constexpr bool IsValid(PermissionDefault policy) noexcept {
  switch (policy) {
    case PermissionDefault::kDeny:
    case PermissionDefault::kAskOnce:
      return true;
  }
  return false;
}

/// Standard privacy defaults for the browser engine.
///
/// All values are chosen most-conservative-first.  The struct is plain data
/// consumed by the CEF settings adapter, the omnibox provider (BUX-04B) and
/// the permission layer (CEF-05); this module performs no I/O itself.
///
/// Consistency rule: privacy always wins on conflict — `Normalize()` folds
/// contradictory combinations upward (e.g. `kBlockAll` cookies forces
/// third-party blocking semantics regardless of any looser hint).
struct PrivacyDefaults final {
  CookiePolicy cookie_policy = CookiePolicy::kBlockThirdParty;
  ReferrerPolicy referrer_policy = ReferrerPolicy::kStrictOriginWhenCrossOrigin;
  PermissionDefault permission_default = PermissionDefault::kDeny;
  /// Partition storage (cache, DOM storage) by top-level site.
  bool storage_partitioning = true;
  /// Upgrade scheme-less omnibox URL input to https.
  bool https_default = true;
};

/// Returns the factory defaults; equivalent to a value-initialized
/// `PrivacyDefaults`, provided for symmetry with loaded configurations.
PrivacyDefaults DefaultPrivacyDefaults() noexcept;

/// Validates a candidate configuration.  Out-of-domain enum values fail
/// closed (return false); callers must then fall back to
/// `DefaultPrivacyDefaults()` rather than partially applying the candidate.
bool Validate(const PrivacyDefaults& candidate) noexcept;

/// Folds conflicting combinations toward higher privacy and returns the
/// normalized copy.  Currently: nothing to fold (each field is independent),
/// but the hook freezes the conflict-resolution point for future policies.
PrivacyDefaults Normalize(const PrivacyDefaults& candidate) noexcept;

/// Deterministic one-line snapshot used as the compatibility golden.
/// Contains only enum names and booleans — never site or user data.
std::string Describe(const PrivacyDefaults& defaults);

}  // namespace crayon::browser_privacy
