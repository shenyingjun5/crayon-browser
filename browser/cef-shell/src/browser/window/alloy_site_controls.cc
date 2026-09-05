#include "browser/window/alloy_site_controls.h"

#include <limits>
#include <utility>
#include <vector>

namespace crayon::browser::cef_shell::window {

namespace {

using browser_site_controls::PermissionKind;
using browser_site_controls::PromptResolution;
using browser_site_controls::SitePermission;

constexpr std::size_t kMaxExternalTargetBytes = 512;

} // namespace

AlloySiteControls::AlloySiteControls(
    permission::PermissionStore *permission_store)
    : permission_store_(permission_store) {}

bool AlloySiteControls::OnNavigation(std::uint64_t generation,
                                     std::string canonical_origin) {
  if (!active_ || generation == 0 || generation <= navigation_generation_ ||
      !browser_site_controls::detail::IsValidSiteOrigin(canonical_origin)) {
    return false;
  }
  navigation_generation_ = generation;
  origin_ = std::move(canonical_origin);
  DenyPending();
  return true;
}

std::optional<std::uint64_t> AlloySiteControls::BeginPermission(
    std::uint64_t generation, const std::string &origin, PermissionKind kind,
    std::uint64_t now, std::uint64_t deadline, DecisionCallback completion) {
  if (!active_ || generation != navigation_generation_ || origin != origin_ ||
      !completion) {
    if (completion)
      completion(false);
    return std::nullopt;
  }
  if (state_.PermissionAt(origin, kind, now) != SitePermission::kDeny) {
    completion(true);
    return 0;
  }
  const auto request_id = NextRequestId();
  if (!request_id || !queue_.Enqueue(origin, kind, now, deadline)) {
    completion(false);
    return std::nullopt;
  }
  permissions_.push_back(PendingPermission{
      AlloyPermissionPrompt{*request_id, generation, origin, kind, deadline},
      std::move(completion)});
  return request_id;
}

AlloySiteControlResult AlloySiteControls::ResolvePermission(
    std::uint64_t request_id, AlloyPermissionDecision decision,
    std::uint64_t now, std::uint64_t expires_at) {
  if (!active_)
    return AlloySiteControlResult::kInactive;
  if (permissions_.empty() ||
      permissions_.front().prompt.request_id != request_id)
    return AlloySiteControlResult::kNotFront;
  const AlloyPermissionPrompt &prompt = permissions_.front().prompt;
  if (prompt.navigation_generation != navigation_generation_ ||
      prompt.origin != origin_) {
    return AlloySiteControlResult::kStaleGeneration;
  }

  SitePermission site_decision = SitePermission::kDeny;
  switch (decision) {
  case AlloyPermissionDecision::kDeny:
    break;
  case AlloyPermissionDecision::kAllowSession:
    site_decision = SitePermission::kAllowSession;
    break;
  case AlloyPermissionDecision::kAllowUntil:
    site_decision = SitePermission::kAllowUntil;
    break;
  default:
    return AlloySiteControlResult::kInvalidInput;
  }
  if (!state_.SetPermission(prompt.origin, prompt.kind, site_decision, now,
                            expires_at)) {
    return AlloySiteControlResult::kInvalidInput;
  }

  PendingPermission pending = std::move(permissions_.front());
  permissions_.pop_front();
  static_cast<void>(queue_.ResolveFront(site_decision == SitePermission::kDeny
                                            ? PromptResolution::kDeny
                                            : PromptResolution::kGrant));
  if (permission_store_) {
    const auto store_kind = StoreKind(pending.prompt.kind);
    if (store_kind) {
      permission_store_->Record(
          pending.prompt.origin, *store_kind,
          site_decision == SitePermission::kAllowSession
              ? permission::PermissionDecision::kAllowSession
              : permission::PermissionDecision::kDeny);
    }
  }
  DecisionCallback completion = std::move(pending.completion);
  completion(site_decision != SitePermission::kDeny);
  return AlloySiteControlResult::kSuccess;
}

std::size_t AlloySiteControls::ExpirePermissions(std::uint64_t now) {
  if (!active_)
    return 0;
  std::vector<DecisionCallback> completions;
  for (auto it = permissions_.begin(); it != permissions_.end();) {
    if (it->prompt.deadline != 0 && it->prompt.deadline <= now) {
      static_cast<void>(queue_.Cancel(it->prompt.origin, it->prompt.kind));
      completions.push_back(std::move(it->completion));
      it = permissions_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto &completion : completions)
    completion(false);
  return completions.size();
}

std::optional<std::uint64_t> AlloySiteControls::BeginCertificateError(
    std::uint64_t generation, browser_site_controls::CertErrorKind kind,
    DecisionCallback completion) {
  if (!active_ || cert_ || generation != navigation_generation_ ||
      !completion ||
      !state_.OnCertificateError(
          kind, generation, browser_site_controls::ControlSource::kEngine)) {
    if (completion)
      completion(false);
    return std::nullopt;
  }
  const auto request_id = NextRequestId();
  if (!request_id) {
    static_cast<void>(state_.DecideCertificateError(
        browser_site_controls::CertDecision::kGoBack));
    completion(false);
    return std::nullopt;
  }
  cert_ = PendingCertificate{*request_id, generation, std::move(completion)};
  return request_id;
}

AlloySiteControlResult AlloySiteControls::ResolveCertificate(
    std::uint64_t request_id, browser_site_controls::CertDecision decision) {
  if (!active_)
    return AlloySiteControlResult::kInactive;
  if (!cert_ || cert_->request_id != request_id)
    return AlloySiteControlResult::kInvalidInput;
  if (cert_->generation != navigation_generation_) {
    return AlloySiteControlResult::kStaleGeneration;
  }
  if (!state_.DecideCertificateError(decision))
    return AlloySiteControlResult::kInvalidInput;
  DecisionCallback completion = std::move(cert_->completion);
  cert_.reset();
  completion(decision == browser_site_controls::CertDecision::kProceedOnce);
  return AlloySiteControlResult::kSuccess;
}

std::optional<std::uint64_t> AlloySiteControls::BeginExternalProtocol(
    std::uint64_t generation, const std::string &origin, std::string scheme,
    std::string display_target, DecisionCallback completion) {
  if (!active_ || external_prompt_ || generation != navigation_generation_ ||
      origin != origin_ || !completion || !IsAllowedExternalScheme(scheme) ||
      !IsValidDisplayTarget(scheme, display_target)) {
    if (completion)
      completion(false);
    return std::nullopt;
  }
  const auto remembered = state_.RememberedProtocolDecision(scheme, origin);
  if (remembered) {
    completion(*remembered ==
               browser_site_controls::ProtocolDecision::kRememberAllow);
    return 0;
  }
  const auto request_id = NextRequestId();
  if (!request_id) {
    completion(false);
    return std::nullopt;
  }
  external_prompt_ =
      AlloyExternalProtocolPrompt{*request_id, generation, origin,
                                  std::move(scheme), std::move(display_target)};
  external_completion_ = std::move(completion);
  return request_id;
}

AlloySiteControlResult AlloySiteControls::ResolveExternalProtocol(
    std::uint64_t request_id,
    browser_site_controls::ProtocolDecision decision) {
  if (!active_)
    return AlloySiteControlResult::kInactive;
  if (!external_prompt_ || external_prompt_->request_id != request_id ||
      !browser_site_controls::IsValid(decision))
    return AlloySiteControlResult::kInvalidInput;
  if (external_prompt_->navigation_generation != navigation_generation_ ||
      external_prompt_->origin != origin_) {
    return AlloySiteControlResult::kStaleGeneration;
  }
  const bool allowed = state_.DecideExternalProtocol(
      external_prompt_->scheme, external_prompt_->origin, decision);
  DecisionCallback completion = std::move(external_completion_);
  external_prompt_.reset();
  external_completion_ = {};
  completion(allowed);
  return AlloySiteControlResult::kSuccess;
}

const AlloyPermissionPrompt *
AlloySiteControls::front_permission() const noexcept {
  return permissions_.empty() ? nullptr : &permissions_.front().prompt;
}

bool AlloySiteControls::Shutdown() {
  if (!active_)
    return true;
  active_ = false;
  DenyPending();
  queue_.Shutdown();
  state_.Shutdown();
  permission_store_ = nullptr;
  origin_.clear();
  navigation_generation_ = 0;
  return true;
}

bool AlloySiteControls::IsAllowedExternalScheme(
    const std::string &scheme) noexcept {
  return scheme == "mailto" || scheme == "tel" || scheme == "sms";
}

bool AlloySiteControls::IsValidDisplayTarget(
    const std::string &scheme, const std::string &target) noexcept {
  if (target.empty() || target.size() > kMaxExternalTargetBytes ||
      target.rfind(scheme + ":", 0) != 0) {
    return false;
  }
  for (unsigned char ch : target) {
    if (ch < 0x20 || ch == 0x7f)
      return false;
  }
  return true;
}

std::optional<permission::PermissionKind>
AlloySiteControls::StoreKind(PermissionKind kind) noexcept {
  switch (kind) {
  case PermissionKind::kCamera:
    return permission::PermissionKind::kCamera;
  case PermissionKind::kMicrophone:
    return permission::PermissionKind::kMicrophone;
  case PermissionKind::kNotifications:
    return permission::PermissionKind::kNotifications;
  case PermissionKind::kGeolocation:
    return permission::PermissionKind::kGeolocation;
  case PermissionKind::kClipboardRead:
    return permission::PermissionKind::kClipboardRead;
  case PermissionKind::kClipboardWrite:
    return permission::PermissionKind::kClipboardWrite;
  case PermissionKind::kDownload:
    return permission::PermissionKind::kDownload;
  }
  return std::nullopt;
}

std::optional<std::uint64_t> AlloySiteControls::NextRequestId() {
  if (next_request_id_ == 0)
    return std::nullopt;
  const std::uint64_t id = next_request_id_;
  next_request_id_ =
      id == (std::numeric_limits<std::uint64_t>::max)() ? 0 : id + 1;
  return id;
}

void AlloySiteControls::DenyPending() {
  std::vector<DecisionCallback> completions;
  while (!permissions_.empty()) {
    completions.push_back(std::move(permissions_.front().completion));
    permissions_.pop_front();
  }
  while (queue_.ResolveFront(PromptResolution::kDismiss)) {
  }
  if (cert_) {
    completions.push_back(std::move(cert_->completion));
    cert_.reset();
    if (state_.HasPendingCertificateError()) {
      static_cast<void>(state_.DecideCertificateError(
          browser_site_controls::CertDecision::kGoBack));
    }
  }
  if (external_prompt_) {
    completions.push_back(std::move(external_completion_));
    external_prompt_.reset();
    external_completion_ = {};
  }
  for (auto &completion : completions) {
    if (completion)
      completion(false);
  }
}

} // namespace crayon::browser::cef_shell::window
