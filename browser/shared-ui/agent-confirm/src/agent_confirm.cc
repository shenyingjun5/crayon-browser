#include "crayon/browser_agent_confirm/agent_confirm.h"

#include <algorithm>
#include <sstream>

namespace crayon::browser_agent_confirm {
namespace {

bool IsTokenChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == ':';
}

}  // namespace

bool IsSensitiveParamKey(const std::string& key) {
  static const char* const kSensitive[] = {"password",   "passwd",   "pwd",    "payment",
                                           "card",       "cvv",      "cookie", "authorization",
                                           "token",      "secret",   "file",   "upload",
                                           "credential"};
  return std::any_of(std::begin(kSensitive), std::end(kSensitive),
                     [&key](const char* family) { return key.find(family) != std::string::npos; });
}

bool IsValidToken(const std::string& value) {
  return !value.empty() && value.size() <= kMaxFieldLen &&
         std::all_of(value.begin(), value.end(), IsTokenChar);
}

std::string AgentConfirmRequest::Fingerprint() const {
  // Stable digest over identity + params; values are represented only
  // by length and sensitivity so nothing secret enters the string.
  std::ostringstream digest;
  digest << client << '|' << tool << '|' << capability << '|' << risk << '|' << target_scope
         << '|' << (discloses_page_data ? 'd' : '-');
  for (const ParamDigest& param : params) {
    digest << '|' << param.key << ':' << param.value_len
           << (param.sensitivity == ParamSensitivity::kSensitive ? 's' : 'p');
  }
  return digest.str();
}

bool AgentConfirmModel::Present(const AgentConfirmRequest& request, std::uint64_t now_ms) {
  if (!IsValidToken(request.client) || !IsValidToken(request.tool) ||
      !IsValidToken(request.capability) || !IsValidToken(request.risk) ||
      request.target_scope.empty() || request.target_scope.size() > kMaxScopeLen) {
    return false;
  }
  if (request.params.size() > kMaxParams) {
    return false;
  }
  for (const ParamDigest& param : request.params) {
    if (!IsValidToken(param.key) || param.key.size() > kMaxParamKeyLen) {
      return false;
    }
  }
  if (request.expires_at_ms <= now_ms) {
    return false;  // already expired at presentation time
  }
  request_ = request;
  fingerprint_ = request.Fingerprint();
  expires_at_ms_ = request.expires_at_ms;
  stale_ = false;
  state_ = ConfirmState::kPending;
  return true;
}

bool AgentConfirmModel::Confirm(std::uint64_t now_ms) {
  if (state_ != ConfirmState::kPending || stale_) {
    return false;
  }
  if (now_ms >= expires_at_ms_) {
    state_ = ConfirmState::kNone;  // expired: drop, require re-present
    return false;
  }
  state_ = ConfirmState::kConfirmed;
  return true;
}

bool AgentConfirmModel::Deny() {
  if (state_ != ConfirmState::kPending || stale_) {
    return false;
  }
  state_ = ConfirmState::kDenied;
  return true;
}

void AgentConfirmModel::OnContextChanged(const std::string& new_fingerprint) {
  if (new_fingerprint == fingerprint_) {
    return;  // no-op for identical context
  }
  if (state_ == ConfirmState::kPending || state_ == ConfirmState::kConfirmed) {
    // AG-004: any change forces re-confirmation.
    stale_ = true;
    state_ = ConfirmState::kNone;
    fingerprint_.clear();
  }
}

void AgentConfirmModel::Tick(std::uint64_t now_ms) {
  if (state_ == ConfirmState::kPending && now_ms >= expires_at_ms_) {
    state_ = ConfirmState::kNone;
  }
}

}  // namespace crayon::browser_agent_confirm
