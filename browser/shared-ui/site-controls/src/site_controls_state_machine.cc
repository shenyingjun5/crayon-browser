#include "crayon/browser_site_controls/site_controls_state_machine.h"

namespace crayon::browser_site_controls {

namespace {

using crayon::browser_navigation::SiteIdentity;

void SetError(SiteControlError* error, SiteControlError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

/// Drops recency entries whose key is no longer present in the live map.
/// Keeps FIFO recency deques bounded under clear/reinsert cycles.
template <typename Map>
void CompactRecency(std::deque<std::string>* recency, const Map& live) {
  std::deque<std::string> kept;
  for (const std::string& key : *recency) {
    if (live.count(key) != 0) {
      kept.push_back(key);
    }
  }
  recency->swap(kept);
}

}  // namespace

bool SiteControlsStateMachine::IsDangerousScheme(
    const std::string& scheme) noexcept {
  return scheme == "javascript" || scheme == "data" || scheme == "vbscript";
}

std::string SiteControlsStateMachine::PermissionKey(
    const std::string& origin,
    PermissionKind kind) {
  return origin + "|" + std::to_string(static_cast<int>(kind));
}

bool SiteControlsStateMachine::SetSiteIdentity(SiteIdentity identity,
                                               ControlSource source) noexcept {
  if (!active_) {
    return false;
  }
  if (source != ControlSource::kEngine) {
    return false;  // Page content can never forge security state.
  }
  if (!crayon::browser_navigation::IsValid(identity)) {
    return false;
  }
  identity_ = identity;
  return true;
}

bool SiteControlsStateMachine::SetPermission(const std::string& origin,
                                             PermissionKind kind,
                                             SitePermission decision,
                                             std::uint64_t now,
                                             std::uint64_t expires_at,
                                             SiteControlError* error) {
  if (!active_ || !detail::IsValidSiteOrigin(origin) || !IsValid(kind) ||
      !IsValid(decision)) {
    SetError(error, SiteControlError::kInvalidInput);
    return false;
  }
  if (decision == SitePermission::kAllowUntil && expires_at <= now) {
    SetError(error, SiteControlError::kInvalidInput);
    return false;
  }
  const std::string key = PermissionKey(origin, kind);
  if (permissions_.count(key) != 0) {
    // Re-recording an existing key refreshes its recency (LRU order).
    for (auto it = permission_recency_.begin();
         it != permission_recency_.end(); ++it) {
      if (*it == key) {
        permission_recency_.erase(it);
        break;
      }
    }
    permission_recency_.push_back(key);
    permissions_[key] = PermissionEntry{decision, expires_at, now};
    return true;
  }
  if (permission_recency_.size() > 2 * kMaxPermissionEntries) {
    CompactRecency(&permission_recency_, permissions_);
  }
  while (permissions_.size() >= kMaxPermissionEntries &&
         !permission_recency_.empty()) {
    // Evict the oldest entry to stay bounded; stale deque entries are
    // skipped harmlessly because erasing a missing key is a no-op.
    permissions_.erase(permission_recency_.front());
    permission_recency_.pop_front();
  }
  permission_recency_.push_back(key);
  permissions_[key] = PermissionEntry{decision, expires_at, now};
  return true;
}

bool SiteControlsStateMachine::ClearPermission(const std::string& origin,
                                               PermissionKind kind) {
  if (!active_) {
    return false;
  }
  return permissions_.erase(PermissionKey(origin, kind)) != 0;
}

SitePermission SiteControlsStateMachine::PermissionAt(
    const std::string& origin,
    PermissionKind kind,
    std::uint64_t now) const noexcept {
  if (!active_) {
    return SitePermission::kDeny;
  }
  const auto it = permissions_.find(PermissionKey(origin, kind));
  if (it == permissions_.end()) {
    return SitePermission::kDeny;
  }
  if (it->second.decision == SitePermission::kAllowUntil &&
      it->second.expires_at <= now) {
    return SitePermission::kDeny;  // Expired grants fall back to deny.
  }
  return it->second.decision;
}

bool SiteControlsStateMachine::OnCertificateError(
    CertErrorKind kind,
    std::uint64_t navigation_generation,
    ControlSource source) noexcept {
  if (!active_ || !IsValid(kind)) {
    return false;
  }
  if (source != ControlSource::kEngine) {
    return false;  // Page content can never forge certificate state.
  }
  pending_cert_error_ = PendingCertError{kind, navigation_generation, false};
  return true;
}

bool SiteControlsStateMachine::DecideCertificateError(
    CertDecision decision) noexcept {
  if (!active_ || !pending_cert_error_.has_value()) {
    return false;
  }
  switch (decision) {
    case CertDecision::kGoBack:
      pending_cert_error_.reset();
      return true;
    case CertDecision::kProceedOnce:
      pending_cert_error_->proceed_once = true;
      return true;
  }
  return false;
}

bool SiteControlsStateMachine::ProceedOnceApplies(
    std::uint64_t navigation_generation) const noexcept {
  if (!active_ || !pending_cert_error_.has_value() ||
      !pending_cert_error_->proceed_once) {
    return false;
  }
  return pending_cert_error_->navigation_generation == navigation_generation;
}

bool SiteControlsStateMachine::DecideExternalProtocol(
    const std::string& scheme,
    const std::string& origin,
    ProtocolDecision decision,
    SiteControlError* error) {
  if (!active_ || scheme.empty() || scheme.size() > kMaxSchemeLength ||
      !detail::IsValidSiteOrigin(origin) || !IsValid(decision)) {
    SetError(error, SiteControlError::kInvalidInput);
    return false;
  }
  if (IsDangerousScheme(scheme)) {
    // Dangerous schemes never reach the confirmation flow.
    SetError(error, SiteControlError::kInvalidInput);
    return false;
  }
  if (decision != ProtocolDecision::kRememberAllow &&
      decision != ProtocolDecision::kRememberDeny) {
    return decision == ProtocolDecision::kAllowOnce;
  }
  const std::string key = scheme + "|" + origin;
  if (protocol_memory_.count(key) == 0) {
    if (protocol_recency_.size() > 2 * kMaxProtocolMemoryEntries) {
      CompactRecency(&protocol_recency_, protocol_memory_);
    }
    while (protocol_memory_.size() >= kMaxProtocolMemoryEntries &&
           !protocol_recency_.empty()) {
      protocol_memory_.erase(protocol_recency_.front());
      protocol_recency_.pop_front();
    }
    protocol_recency_.push_back(key);
  }
  protocol_memory_[key] = decision;
  return decision == ProtocolDecision::kRememberAllow;
}

std::optional<ProtocolDecision>
SiteControlsStateMachine::RememberedProtocolDecision(
    const std::string& scheme,
    const std::string& origin) const {
  if (!active_) {
    return std::nullopt;
  }
  const auto it = protocol_memory_.find(scheme + "|" + origin);
  if (it == protocol_memory_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void SiteControlsStateMachine::Shutdown() noexcept {
  active_ = false;
  identity_ = SiteIdentity::kUnknown;
  permissions_.clear();
  permission_recency_.clear();
  pending_cert_error_.reset();
  protocol_memory_.clear();
  protocol_recency_.clear();
}

}  // namespace crayon::browser_site_controls
