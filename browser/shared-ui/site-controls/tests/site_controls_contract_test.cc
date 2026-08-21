#include <iostream>
#include <string>

#include "crayon/browser_site_controls/permission_prompt_queue.h"
#include "crayon/browser_site_controls/site_controls_state_machine.h"

namespace {

using crayon::browser_navigation::SiteIdentity;
using crayon::browser_site_controls::CertDecision;
using crayon::browser_site_controls::CertErrorKind;
using crayon::browser_site_controls::ControlSource;
using crayon::browser_site_controls::kMaxPendingPrompts;
using crayon::browser_site_controls::kMaxPermissionEntries;
using crayon::browser_site_controls::kMaxProtocolMemoryEntries;
using crayon::browser_site_controls::PermissionKind;
using crayon::browser_site_controls::PermissionPromptQueue;
using crayon::browser_site_controls::PromptResolution;
using crayon::browser_site_controls::ProtocolDecision;
using crayon::browser_site_controls::SiteControlError;
using crayon::browser_site_controls::SiteControlsStateMachine;
using crayon::browser_site_controls::SitePermission;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

std::string Origin(std::size_t index) {
  return "https://host-" + std::to_string(index) + ".example";
}

bool SiteIdentityIsEngineOnly() {
  SiteControlsStateMachine sm;
  CHECK(sm.SetSiteIdentity(SiteIdentity::kSecure, ControlSource::kEngine));
  CHECK(sm.site_identity() == SiteIdentity::kSecure);
  // Page content can never forge security identity.
  CHECK(!sm.SetSiteIdentity(SiteIdentity::kDangerous,
                            ControlSource::kPageContent));
  CHECK(sm.site_identity() == SiteIdentity::kSecure);
  // Out-of-range enum is rejected.
  CHECK(!sm.SetSiteIdentity(static_cast<SiteIdentity>(99),
                            ControlSource::kEngine));
  return true;
}

bool PermissionInputValidation() {
  SiteControlsStateMachine sm;
  SiteControlError error = SiteControlError::kUnknownEntry;
  CHECK(!sm.SetPermission("", PermissionKind::kCamera,
                          SitePermission::kAllowSession, 10, 0, &error));
  CHECK(error == SiteControlError::kInvalidInput);
  CHECK(!sm.SetPermission("ftp://host.example", PermissionKind::kCamera,
                          SitePermission::kAllowSession, 10, 0));
  CHECK(!sm.SetPermission("https://user@host.example", PermissionKind::kCamera,
                          SitePermission::kAllowSession, 10, 0));
  CHECK(!sm.SetPermission(std::string(300, 'h'), PermissionKind::kCamera,
                          SitePermission::kAllowSession, 10, 0));
  CHECK(!sm.SetPermission("https://host.example",
                          static_cast<PermissionKind>(77),
                          SitePermission::kAllowSession, 10, 0));
  CHECK(!sm.SetPermission("https://host.example", PermissionKind::kCamera,
                          static_cast<SitePermission>(9), 10, 0));
  // kAllowUntil requires a future expiry.
  CHECK(!sm.SetPermission("https://host.example", PermissionKind::kCamera,
                          SitePermission::kAllowUntil, 10, 10));
  CHECK(!sm.SetPermission("https://host.example", PermissionKind::kCamera,
                          SitePermission::kAllowUntil, 10, 5));
  CHECK(sm.SetPermission("https://host.example", PermissionKind::kCamera,
                         SitePermission::kAllowUntil, 10, 11));
  CHECK(sm.permission_entry_count() == 1);
  return true;
}

bool PermissionTtlExpiry() {
  SiteControlsStateMachine sm;
  CHECK(sm.SetPermission("https://a.example", PermissionKind::kMicrophone,
                         SitePermission::kAllowUntil, 10, 100));
  CHECK(sm.PermissionAt("https://a.example", PermissionKind::kMicrophone,
                        99) == SitePermission::kAllowUntil);
  // At the expiry instant the grant counts as denied.
  CHECK(sm.PermissionAt("https://a.example", PermissionKind::kMicrophone,
                        100) == SitePermission::kDeny);
  CHECK(sm.PermissionAt("https://a.example", PermissionKind::kMicrophone,
                        1000) == SitePermission::kDeny);
  // Session grants never expire.
  CHECK(sm.SetPermission("https://a.example", PermissionKind::kCamera,
                         SitePermission::kAllowSession, 10, 0));
  CHECK(sm.PermissionAt("https://a.example", PermissionKind::kCamera,
                        100000) == SitePermission::kAllowSession);
  // Unknown pairs default to deny.
  CHECK(sm.PermissionAt("https://unknown.example", PermissionKind::kCamera,
                        10) == SitePermission::kDeny);
  return true;
}

bool PermissionCapacityAndEviction() {
  SiteControlsStateMachine sm;
  for (std::size_t i = 0; i < kMaxPermissionEntries; ++i) {
    CHECK(sm.SetPermission(Origin(i), PermissionKind::kCamera,
                           SitePermission::kAllowSession, i, 0));
  }
  CHECK(sm.permission_entry_count() == kMaxPermissionEntries);
  // Re-recording an existing key refreshes it without eviction.
  CHECK(sm.SetPermission(Origin(0), PermissionKind::kCamera,
                         SitePermission::kAllowUntil, 500, 600));
  CHECK(sm.permission_entry_count() == kMaxPermissionEntries);
  CHECK(sm.PermissionAt(Origin(1), PermissionKind::kCamera, 500) ==
        SitePermission::kAllowSession);
  // A brand-new key evicts the least-recently-recorded entry.
  CHECK(sm.SetPermission(Origin(kMaxPermissionEntries),
                         PermissionKind::kCamera,
                         SitePermission::kAllowSession, 501, 0));
  CHECK(sm.permission_entry_count() == kMaxPermissionEntries);
  CHECK(sm.PermissionAt(Origin(1), PermissionKind::kCamera, 501) ==
        SitePermission::kDeny);
  // The refreshed entry survives eviction.
  CHECK(sm.PermissionAt(Origin(0), PermissionKind::kCamera, 501) ==
        SitePermission::kAllowUntil);
  CHECK(sm.PermissionAt(Origin(kMaxPermissionEntries), PermissionKind::kCamera,
                        501) == SitePermission::kAllowSession);
  return true;
}

bool ClearPermissionSemantics() {
  SiteControlsStateMachine sm;
  CHECK(sm.SetPermission("https://a.example", PermissionKind::kGeolocation,
                         SitePermission::kAllowSession, 1, 0));
  CHECK(sm.ClearPermission("https://a.example",
                           PermissionKind::kGeolocation));
  CHECK(sm.PermissionAt("https://a.example", PermissionKind::kGeolocation,
                        2) == SitePermission::kDeny);
  CHECK(!sm.ClearPermission("https://a.example",
                            PermissionKind::kGeolocation));
  // Clearing and re-adding in a loop keeps the store bounded.
  for (std::size_t i = 0; i < 4 * kMaxPermissionEntries; ++i) {
    CHECK(sm.SetPermission("https://cycle.example", PermissionKind::kDownload,
                           SitePermission::kAllowSession, i, 0));
    CHECK(sm.ClearPermission("https://cycle.example",
                             PermissionKind::kDownload));
  }
  CHECK(sm.permission_entry_count() == 0);
  return true;
}

bool PromptQueueFifoDedupCapacity() {
  PermissionPromptQueue queue;
  for (std::size_t i = 0; i < kMaxPendingPrompts; ++i) {
    CHECK(queue.Enqueue(Origin(i), PermissionKind::kCamera, i, 0));
  }
  CHECK(queue.size() == kMaxPendingPrompts);
  // Full queue rejects without side effects.
  CHECK(!queue.Enqueue(Origin(99), PermissionKind::kCamera, 99, 0));
  CHECK(queue.size() == kMaxPendingPrompts);
  // Duplicate pending (origin, kind) rejects without side effects.
  CHECK(!queue.Enqueue(Origin(0), PermissionKind::kCamera, 100, 0));
  CHECK(queue.size() == kMaxPendingPrompts);
  // Same origin with a different kind is a distinct prompt.
  CHECK(!queue.Enqueue(Origin(0), PermissionKind::kMicrophone, 100, 0));
  // Only the front prompt can be resolved (FIFO order preserved).
  const auto* front = queue.front();
  CHECK(front != nullptr);
  CHECK(front->origin == Origin(0));
  CHECK(queue.ResolveFront(PromptResolution::kGrant));
  front = queue.front();
  CHECK(front != nullptr);
  CHECK(front->origin == Origin(1));
  CHECK(queue.ResolveFront(PromptResolution::kDeny));
  CHECK(queue.ResolveFront(PromptResolution::kDismiss));
  CHECK(!queue.empty());
  CHECK(queue.ResolveFront(PromptResolution::kDismiss));
  CHECK(queue.empty());
  CHECK(queue.front() == nullptr);
  CHECK(!queue.ResolveFront(PromptResolution::kGrant));
  return true;
}

bool PromptCancelAndTimeout() {
  PermissionPromptQueue queue;
  CHECK(queue.Enqueue("https://a.example", PermissionKind::kCamera, 1, 50));
  CHECK(queue.Enqueue("https://b.example", PermissionKind::kMicrophone, 2, 0));
  CHECK(queue.Enqueue("https://c.example", PermissionKind::kGeolocation, 3,
                      60));
  // Cancel removes a middle prompt without touching the others.
  CHECK(queue.Cancel("https://b.example", PermissionKind::kMicrophone));
  CHECK(!queue.Cancel("https://b.example", PermissionKind::kMicrophone));
  CHECK(queue.size() == 2);
  const auto* front = queue.front();
  CHECK(front != nullptr);
  CHECK(front->origin == "https://a.example");
  // Timeout removes only prompts whose deadline has passed.
  CHECK(queue.ExpireTimedOut(49) == 0);
  CHECK(queue.ExpireTimedOut(50) == 1);
  CHECK(queue.size() == 1);
  front = queue.front();
  CHECK(front != nullptr);
  CHECK(front->origin == "https://c.example");
  // A deadline in the past at enqueue time is rejected.
  CHECK(!queue.Enqueue("https://d.example", PermissionKind::kCamera, 100, 90));
  // Invalid origin / enum are rejected.
  CHECK(!queue.Enqueue("javascript:bad", PermissionKind::kCamera, 1, 0));
  CHECK(!queue.Enqueue("https://d.example", static_cast<PermissionKind>(77), 1,
                       0));
  return true;
}

bool CertificateErrorFlow() {
  SiteControlsStateMachine sm;
  // Page content cannot raise a certificate error.
  CHECK(!sm.OnCertificateError(CertErrorKind::kExpired, 7,
                               ControlSource::kPageContent));
  CHECK(!sm.HasPendingCertificateError());
  // Invalid kind is rejected.
  CHECK(!sm.OnCertificateError(static_cast<CertErrorKind>(9), 7,
                               ControlSource::kEngine));
  CHECK(sm.OnCertificateError(CertErrorKind::kNameMismatch, 7,
                              ControlSource::kEngine));
  CHECK(sm.HasPendingCertificateError());
  // Proceed-once binds to the navigation generation it was granted on.
  CHECK(sm.DecideCertificateError(CertDecision::kProceedOnce));
  CHECK(sm.ProceedOnceApplies(7));
  CHECK(!sm.ProceedOnceApplies(8));
  CHECK(!sm.ProceedOnceApplies(6));
  // A new error supersedes the old grant.
  CHECK(sm.OnCertificateError(CertErrorKind::kUntrusted, 9,
                              ControlSource::kEngine));
  CHECK(!sm.ProceedOnceApplies(7));
  // Go-back clears the pending error; deciding twice fails.
  CHECK(sm.DecideCertificateError(CertDecision::kGoBack));
  CHECK(!sm.HasPendingCertificateError());
  CHECK(!sm.DecideCertificateError(CertDecision::kGoBack));
  return true;
}

bool ExternalProtocolFlow() {
  SiteControlsStateMachine sm;
  // Dangerous schemes never enter the confirmation flow and are never
  // remembered.
  CHECK(!sm.DecideExternalProtocol("javascript", "https://a.example",
                                   ProtocolDecision::kAllowOnce));
  CHECK(!sm.DecideExternalProtocol("data", "https://a.example",
                                   ProtocolDecision::kRememberAllow));
  CHECK(!sm.DecideExternalProtocol("vbscript", "https://a.example",
                                   ProtocolDecision::kRememberDeny));
  CHECK(!sm.RememberedProtocolDecision("data", "https://a.example")
             .has_value());
  // Allow-once grants without remembering.
  CHECK(sm.DecideExternalProtocol("mailto", "https://a.example",
                                  ProtocolDecision::kAllowOnce));
  CHECK(!sm.RememberedProtocolDecision("mailto", "https://a.example")
             .has_value());
  // Plain deny grants nothing and remembers nothing.
  CHECK(!sm.DecideExternalProtocol("mailto", "https://a.example",
                                   ProtocolDecision::kDeny));
  // Remember decisions persist per (scheme, origin).
  CHECK(sm.DecideExternalProtocol("mailto", "https://a.example",
                                  ProtocolDecision::kRememberAllow));
  CHECK(sm.RememberedProtocolDecision("mailto", "https://a.example") ==
        ProtocolDecision::kRememberAllow);
  CHECK(!sm.DecideExternalProtocol("tel", "https://a.example",
                                   ProtocolDecision::kRememberDeny));
  CHECK(sm.RememberedProtocolDecision("tel", "https://a.example") ==
        ProtocolDecision::kRememberDeny);
  // A different origin has no memory.
  CHECK(!sm.RememberedProtocolDecision("mailto", "https://b.example")
             .has_value());
  // Invalid scheme/origin/decision are rejected.
  CHECK(!sm.DecideExternalProtocol(std::string(100, 's'), "https://a.example",
                                   ProtocolDecision::kAllowOnce));
  CHECK(!sm.DecideExternalProtocol("mailto", "ftp://a.example",
                                   ProtocolDecision::kAllowOnce));
  CHECK(!sm.DecideExternalProtocol("mailto", "https://a.example",
                                   static_cast<ProtocolDecision>(9)));
  return true;
}

bool ProtocolMemoryIsBounded() {
  SiteControlsStateMachine sm;
  for (std::size_t i = 0; i < 2 * kMaxProtocolMemoryEntries; ++i) {
    CHECK(sm.DecideExternalProtocol("scheme" + std::to_string(i),
                                    "https://a.example",
                                    ProtocolDecision::kRememberAllow));
  }
  CHECK(sm.protocol_memory_count() == kMaxProtocolMemoryEntries);
  return true;
}

bool ShutdownRejectsEverything() {
  SiteControlsStateMachine sm;
  PermissionPromptQueue queue;
  CHECK(sm.SetSiteIdentity(SiteIdentity::kSecure, ControlSource::kEngine));
  CHECK(sm.SetPermission("https://a.example", PermissionKind::kCamera,
                         SitePermission::kAllowSession, 1, 0));
  CHECK(queue.Enqueue("https://a.example", PermissionKind::kCamera, 1, 0));
  sm.Shutdown();
  queue.Shutdown();
  CHECK(!sm.active());
  CHECK(!queue.active());
  CHECK(sm.site_identity() == SiteIdentity::kUnknown);
  CHECK(sm.PermissionAt("https://a.example", PermissionKind::kCamera, 2) ==
        SitePermission::kDeny);
  CHECK(!sm.SetSiteIdentity(SiteIdentity::kSecure, ControlSource::kEngine));
  CHECK(!sm.SetPermission("https://a.example", PermissionKind::kCamera,
                          SitePermission::kAllowSession, 2, 0));
  CHECK(!sm.ClearPermission("https://a.example", PermissionKind::kCamera));
  CHECK(!sm.OnCertificateError(CertErrorKind::kGeneric, 1,
                               ControlSource::kEngine));
  CHECK(!sm.DecideCertificateError(CertDecision::kGoBack));
  CHECK(!sm.DecideExternalProtocol("mailto", "https://a.example",
                                   ProtocolDecision::kAllowOnce));
  CHECK(!sm.RememberedProtocolDecision("mailto", "https://a.example")
             .has_value());
  CHECK(!queue.Enqueue("https://a.example", PermissionKind::kCamera, 2, 0));
  CHECK(!queue.ResolveFront(PromptResolution::kGrant));
  CHECK(!queue.Cancel("https://a.example", PermissionKind::kCamera));
  CHECK(queue.ExpireTimedOut(100) == 0);
  CHECK(queue.empty());
  return true;
}

bool EnumClosure() {
  using crayon::browser_site_controls::IsValid;
  CHECK(!IsValid(static_cast<SitePermission>(3)));
  CHECK(!IsValid(static_cast<PermissionKind>(7)));
  CHECK(!IsValid(static_cast<CertErrorKind>(4)));
  CHECK(!IsValid(static_cast<ProtocolDecision>(4)));
  CHECK(!IsValid(static_cast<PromptResolution>(3)));
  CHECK(IsValid(SitePermission::kDeny));
  CHECK(IsValid(PermissionKind::kDownload));
  CHECK(IsValid(CertErrorKind::kGeneric));
  CHECK(IsValid(ProtocolDecision::kRememberDeny));
  CHECK(IsValid(PromptResolution::kDismiss));
  return true;
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    bool (*run)();
  };
  const TestCase kTests[] = {
      {"SiteIdentityIsEngineOnly", &SiteIdentityIsEngineOnly},
      {"PermissionInputValidation", &PermissionInputValidation},
      {"PermissionTtlExpiry", &PermissionTtlExpiry},
      {"PermissionCapacityAndEviction", &PermissionCapacityAndEviction},
      {"ClearPermissionSemantics", &ClearPermissionSemantics},
      {"PromptQueueFifoDedupCapacity", &PromptQueueFifoDedupCapacity},
      {"PromptCancelAndTimeout", &PromptCancelAndTimeout},
      {"CertificateErrorFlow", &CertificateErrorFlow},
      {"ExternalProtocolFlow", &ExternalProtocolFlow},
      {"ProtocolMemoryIsBounded", &ProtocolMemoryIsBounded},
      {"ShutdownRejectsEverything", &ShutdownRejectsEverything},
      {"EnumClosure", &EnumClosure},
  };
  for (const TestCase& test : kTests) {
    if (!test.run()) {
      std::cerr << "FAILED: " << test.name << '\n';
      return EXIT_FAILURE;
    }
  }
  std::cout << "site_controls_contract: all tests passed\n";
  return EXIT_SUCCESS;
}
