// HUB-06 capability route preview model implementation.
#include "crayon/browser_capability_route/capability_route.h"

namespace crayon::browser_capability_route {
namespace {

bool AllBytesIn(const std::string& value, bool (*predicate)(unsigned char)) {
  for (unsigned char byte : value) {
    if (!predicate(byte)) {
      return false;
    }
  }
  return true;
}

bool IsLowerAlnum(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9');
}

bool IsTokenByte(unsigned char byte) {
  return IsLowerAlnum(byte) || byte == '_' || byte == '.' || byte == ':' ||
         byte == '-';
}

}  // namespace

bool RouteKinds::IsValid(const std::string& value) {
  return value == kPartner || value == kSiteSkill ||
         value == kWebAutomation || value == kHumanHandoff ||
         value == kReject;
}

bool IsValidReason(const std::string& value) {
  // HUB-04 PolicyReason wire names.
  return value == "not_evaluated" || value == "selected_by_default_rank" ||
         value == "selected_by_user_preference" ||
         value == "all_candidates_excluded" || value == "no_candidates";
}

bool IsValidTrust(const std::string& value) {
  // Domain TrustLevel wire names.
  return value == "untrusted" || value == "user_approved" ||
         value == "system";
}

bool IsValidToken(const std::string& value) {
  return !value.empty() && value.size() <= kMaxIdLen &&
         AllBytesIn(value, &IsTokenByte);
}

bool CapabilityRouteModel::Present(const CapabilityRoutePreview& preview) {
  if (preview.candidates.size() > kMaxRows ||
      preview.exclusions.size() > kMaxRows) {
    return false;
  }
  if (!preview.selected_kind.empty()) {
    if (!preview.selected_id.empty() &&
        !IsValidToken(preview.selected_id)) {
      return false;
    }
    if (!RouteKinds::IsValid(preview.selected_kind)) {
      return false;
    }
  } else if (!preview.selected_id.empty()) {
    return false;
  }
  if (!IsValidReason(preview.reason)) {
    return false;
  }
  for (const RouteCandidateView& candidate : preview.candidates) {
    if (!IsValidToken(candidate.capability_id) ||
        candidate.version.empty() ||
        candidate.version.size() > kMaxVersionLen ||
        !RouteKinds::IsValid(candidate.kind) ||
        !IsValidTrust(candidate.trust)) {
      return false;
    }
  }
  for (const RouteExclusionView& exclusion : preview.exclusions) {
    if (!IsValidToken(exclusion.capability_id)) {
      return false;
    }
    if (exclusion.reason != "insufficient_trust" &&
        exclusion.reason != "external_data_forbidden") {
      return false;
    }
  }
  preview_ = preview;
  state_ = RouteState::kPresented;
  override_ = RouteOverride{};
  ++revision_;
  return true;
}

bool CapabilityRouteModel::ApplyOverride(
    const RouteOverride& override_request) {
  if (state_ != RouteState::kPresented) {
    return false;
  }
  if (override_request.present) {
    const std::string& prefer = override_request.prefer_kind;
    // "reject" is a verdict, never a preference.
    if (prefer == RouteKinds::kReject || (!prefer.empty() &&
                                          !RouteKinds::IsValid(prefer))) {
      return false;
    }
  }
  override_ = override_request;
  return true;
}

bool CapabilityRouteModel::Proceed(RouteOverride* out_effective) {
  if (state_ != RouteState::kPresented) {
    return false;
  }
  if (override_.present && out_effective != nullptr) {
    *out_effective = override_;
  }
  override_ = RouteOverride{};
  state_ = RouteState::kProceeded;
  return true;
}

bool CapabilityRouteModel::Cancel() {
  if (state_ != RouteState::kPresented) {
    return false;
  }
  override_ = RouteOverride{};
  state_ = RouteState::kCancelled;
  return true;
}

std::string CapabilityRouteModel::Summary() const {
  std::string summary;
  summary.reserve(256);
  summary += "selected|";
  summary += preview_.selected_kind.empty() ? std::string("none") : preview_.selected_kind;
  summary += "|";
  summary += preview_.selected_id.empty() ? std::string("-") : preview_.selected_id;
  summary += "|";
  summary += preview_.reason;
  summary += "\n";
  for (const RouteCandidateView& candidate : preview_.candidates) {
    summary += "candidate|";
    summary += candidate.kind;
    summary += "|";
    summary += candidate.capability_id;
    summary += "|";
    summary += candidate.trust;
    summary += candidate.sends_data_external ? "|external\n" : "|local\n";
  }
  for (const RouteExclusionView& exclusion : preview_.exclusions) {
    summary += "excluded|";
    summary += exclusion.capability_id;
    summary += "|";
    summary += exclusion.reason;
    summary += "\n";
  }
  return summary;
}

}  // namespace crayon::browser_capability_route
