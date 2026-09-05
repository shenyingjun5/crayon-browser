#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

#include "browser/permission/permission_store.h"
#include "crayon/browser_site_controls/permission_prompt_queue.h"
#include "crayon/browser_site_controls/site_controls_state_machine.h"

namespace crayon::browser::cef_shell::window {

enum class AlloySiteControlResult {
  kSuccess = 0,
  kInvalidInput,
  kBusy,
  kNotFront,
  kStaleGeneration,
  kInactive,
};

enum class AlloyPermissionDecision {
  kDeny = 0,
  kAllowSession,
  kAllowUntil,
};

struct AlloyPermissionPrompt final {
  std::uint64_t request_id = 0;
  std::uint64_t navigation_generation = 0;
  std::string origin;
  browser_site_controls::PermissionKind kind =
      browser_site_controls::PermissionKind::kCamera;
  std::uint64_t deadline = 0;
};

struct AlloyExternalProtocolPrompt final {
  std::uint64_t request_id = 0;
  std::uint64_t navigation_generation = 0;
  std::string origin;
  std::string scheme;
  std::string display_target;
};

/// Per-view coordinator for trusted security prompts.
///
/// The Browser process supplies canonical origins, navigation generations and
/// one-shot completion callbacks. Page content can request a capability but
/// cannot resolve a prompt, manufacture a generation or write security state.
/// Thread contract: single-threaded, CEF UI thread only.
class AlloySiteControls final {
public:
  using DecisionCallback = std::function<void(bool)>;

  explicit AlloySiteControls(permission::PermissionStore *permission_store);

  bool OnNavigation(std::uint64_t generation, std::string canonical_origin);

  std::optional<std::uint64_t>
  BeginPermission(std::uint64_t generation, const std::string &origin,
                  browser_site_controls::PermissionKind kind, std::uint64_t now,
                  std::uint64_t deadline, DecisionCallback completion);
  AlloySiteControlResult ResolvePermission(std::uint64_t request_id,
                                           AlloyPermissionDecision decision,
                                           std::uint64_t now,
                                           std::uint64_t expires_at = 0);
  std::size_t ExpirePermissions(std::uint64_t now);

  std::optional<std::uint64_t>
  BeginCertificateError(std::uint64_t generation,
                        browser_site_controls::CertErrorKind kind,
                        DecisionCallback completion);
  AlloySiteControlResult
  ResolveCertificate(std::uint64_t request_id,
                     browser_site_controls::CertDecision decision);

  std::optional<std::uint64_t>
  BeginExternalProtocol(std::uint64_t generation, const std::string &origin,
                        std::string scheme, std::string display_target,
                        DecisionCallback completion);
  AlloySiteControlResult
  ResolveExternalProtocol(std::uint64_t request_id,
                          browser_site_controls::ProtocolDecision decision);

  const AlloyPermissionPrompt *front_permission() const noexcept;
  const std::optional<AlloyExternalProtocolPrompt> &
  external_protocol_prompt() const noexcept {
    return external_prompt_;
  }
  bool certificate_pending() const noexcept { return cert_.has_value(); }
  const browser_site_controls::SiteControlsStateMachine &state() const {
    return state_;
  }
  bool Shutdown();

private:
  struct PendingPermission final {
    AlloyPermissionPrompt prompt;
    DecisionCallback completion;
  };
  struct PendingCertificate final {
    std::uint64_t request_id = 0;
    std::uint64_t generation = 0;
    DecisionCallback completion;
  };

  static bool IsAllowedExternalScheme(const std::string &scheme) noexcept;
  static bool IsValidDisplayTarget(const std::string &scheme,
                                   const std::string &target) noexcept;
  static std::optional<permission::PermissionKind>
  StoreKind(browser_site_controls::PermissionKind kind) noexcept;
  std::optional<std::uint64_t> NextRequestId();
  void DenyPending();

  permission::PermissionStore *permission_store_ = nullptr;
  browser_site_controls::SiteControlsStateMachine state_;
  browser_site_controls::PermissionPromptQueue queue_;
  std::deque<PendingPermission> permissions_;
  std::optional<PendingCertificate> cert_;
  std::optional<AlloyExternalProtocolPrompt> external_prompt_;
  DecisionCallback external_completion_;
  std::string origin_;
  std::uint64_t navigation_generation_ = 0;
  std::uint64_t next_request_id_ = 1;
  bool active_ = true;
};

} // namespace crayon::browser::cef_shell::window
