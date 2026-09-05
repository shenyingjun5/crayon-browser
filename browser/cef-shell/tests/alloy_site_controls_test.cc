#include <cstdlib>
#include <iostream>
#include <vector>

#include "browser/window/alloy_site_controls.h"

namespace {

using crayon::browser::cef_shell::permission::PermissionDecision;
using crayon::browser::cef_shell::permission::PermissionStore;
using crayon::browser::cef_shell::window::AlloyPermissionDecision;
using crayon::browser::cef_shell::window::AlloySiteControlResult;
using crayon::browser::cef_shell::window::AlloySiteControls;
using crayon::browser_site_controls::CertDecision;
using crayon::browser_site_controls::CertErrorKind;
using crayon::browser_site_controls::PermissionKind;
using crayon::browser_site_controls::ProtocolDecision;
using crayon::browser_site_controls::SitePermission;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

bool PermissionPromptContract() {
  PermissionStore store;
  AlloySiteControls controls(&store);
  std::vector<bool> decisions;
  CHECK(!controls.OnNavigation(0, "https://a.example"));
  CHECK(controls.OnNavigation(1, "https://a.example"));
  CHECK(!controls.OnNavigation(1, "https://rollback.example"));
  CHECK(!controls.BeginPermission(
      1, "https://other.example", PermissionKind::kCamera, 10, 20,
      [&](bool allowed) { decisions.push_back(allowed); }));
  CHECK(decisions.size() == 1 && !decisions.back());

  const auto camera = controls.BeginPermission(
      1, "https://a.example", PermissionKind::kCamera, 10, 20,
      [&](bool allowed) { decisions.push_back(allowed); });
  const auto microphone = controls.BeginPermission(
      1, "https://a.example", PermissionKind::kMicrophone, 10, 21,
      [&](bool allowed) { decisions.push_back(allowed); });
  const auto location = controls.BeginPermission(
      1, "https://a.example", PermissionKind::kGeolocation, 10, 22,
      [&](bool allowed) { decisions.push_back(allowed); });
  const auto notification = controls.BeginPermission(
      1, "https://a.example", PermissionKind::kNotifications, 10, 23,
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(camera && microphone && location && notification);
  CHECK(!controls.BeginPermission(
      1, "https://a.example", PermissionKind::kClipboardRead, 10, 24,
      [&](bool allowed) { decisions.push_back(allowed); }));
  CHECK(!decisions.back());
  CHECK(controls.front_permission() &&
        controls.front_permission()->request_id == *camera);
  CHECK(controls.ResolvePermission(*microphone,
                                   AlloyPermissionDecision::kAllowSession,
                                   11) == AlloySiteControlResult::kNotFront);
  CHECK(controls.ResolvePermission(*camera,
                                   AlloyPermissionDecision::kAllowSession,
                                   11) == AlloySiteControlResult::kSuccess);
  CHECK(decisions.back());
  CHECK(store.Query(
            "https://a.example",
            crayon::browser::cef_shell::permission::PermissionKind::kCamera) ==
        PermissionDecision::kAllowSession);

  CHECK(controls.ResolvePermission(
            *microphone, AlloyPermissionDecision::kAllowUntil, 12, 12) ==
        AlloySiteControlResult::kInvalidInput);
  CHECK(controls.front_permission()->request_id == *microphone);
  CHECK(controls.ResolvePermission(*microphone,
                                   AlloyPermissionDecision::kAllowUntil, 12,
                                   18) == AlloySiteControlResult::kSuccess);
  CHECK(decisions.back());
  CHECK(controls.state().PermissionAt("https://a.example",
                                      PermissionKind::kMicrophone,
                                      17) == SitePermission::kAllowUntil);
  CHECK(controls.state().PermissionAt("https://a.example",
                                      PermissionKind::kMicrophone,
                                      18) == SitePermission::kDeny);
  CHECK(store.Query("https://a.example",
                    crayon::browser::cef_shell::permission::PermissionKind::
                        kMicrophone) == PermissionDecision::kDeny);

  CHECK(controls.ExpirePermissions(22) == 1);
  CHECK(!decisions.back());
  CHECK(controls.front_permission()->request_id == *notification);
  CHECK(controls.OnNavigation(2, "https://b.example"));
  CHECK(!controls.OnNavigation(1, "https://rollback.example"));
  CHECK(!decisions.back());
  CHECK(controls.front_permission() == nullptr);

  const auto cached = controls.BeginPermission(
      2, "https://b.example", PermissionKind::kCamera, 30, 40,
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(cached && *cached != 0);
  CHECK(controls.ResolvePermission(*cached, AlloyPermissionDecision::kDeny,
                                   31) == AlloySiteControlResult::kSuccess);
  CHECK(!decisions.back());
  return true;
}

bool CertificateAndProtocolContract() {
  PermissionStore store;
  AlloySiteControls controls(&store);
  std::vector<bool> decisions;
  CHECK(controls.OnNavigation(7, "https://a.example"));
  const auto cert = controls.BeginCertificateError(
      7, CertErrorKind::kNameMismatch,
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(cert && controls.certificate_pending());
  CHECK(controls.ResolveCertificate(*cert + 1, CertDecision::kProceedOnce) ==
        AlloySiteControlResult::kInvalidInput);
  CHECK(controls.ResolveCertificate(*cert, CertDecision::kProceedOnce) ==
        AlloySiteControlResult::kSuccess);
  CHECK(decisions.back());

  const auto cancelled_cert = controls.BeginCertificateError(
      7, CertErrorKind::kExpired,
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(cancelled_cert);
  CHECK(controls.OnNavigation(8, "https://a.example"));
  CHECK(!decisions.back() && !controls.certificate_pending());

  CHECK(!controls.BeginExternalProtocol(
      8, "https://a.example", "javascript", "javascript:alert(1)",
      [&](bool allowed) { decisions.push_back(allowed); }));
  CHECK(!decisions.back());
  CHECK(!controls.BeginExternalProtocol(
      8, "https://a.example", "MAILTO", "MAILTO:user@example.com",
      [&](bool allowed) { decisions.push_back(allowed); }));
  CHECK(!decisions.back());
  const auto mail = controls.BeginExternalProtocol(
      8, "https://a.example", "mailto", "mailto:user@example.com",
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(mail && controls.external_protocol_prompt());
  CHECK(controls.ResolveExternalProtocol(*mail,
                                         static_cast<ProtocolDecision>(99)) ==
        AlloySiteControlResult::kInvalidInput);
  CHECK(controls.external_protocol_prompt());
  CHECK(controls.ResolveExternalProtocol(*mail,
                                         ProtocolDecision::kRememberAllow) ==
        AlloySiteControlResult::kSuccess);
  CHECK(decisions.back());
  const auto remembered = controls.BeginExternalProtocol(
      8, "https://a.example", "mailto", "mailto:other@example.com",
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(remembered && *remembered == 0 && decisions.back());

  CHECK(controls.OnNavigation(9, "https://b.example"));
  const auto tel = controls.BeginExternalProtocol(
      9, "https://b.example", "tel", "tel:+12025550123",
      [&](bool allowed) { decisions.push_back(allowed); });
  CHECK(tel);
  CHECK(controls.OnNavigation(10, "https://c.example"));
  CHECK(!decisions.back() && !controls.external_protocol_prompt());
  return true;
}

bool ShutdownContract() {
  PermissionStore store;
  AlloySiteControls controls(&store);
  bool resolved = true;
  CHECK(controls.OnNavigation(1, "https://a.example"));
  CHECK(controls.BeginPermission(1, "https://a.example",
                                 PermissionKind::kClipboardWrite, 1, 0,
                                 [&](bool allowed) { resolved = allowed; }));
  CHECK(controls.Shutdown() && controls.Shutdown());
  CHECK(!resolved);
  CHECK(!controls.OnNavigation(2, "https://b.example"));
  CHECK(controls.ResolvePermission(1, AlloyPermissionDecision::kDeny, 2) ==
        AlloySiteControlResult::kInactive);
  return true;
}

} // namespace

int main() {
  return PermissionPromptContract() && CertificateAndProtocolContract() &&
                 ShutdownContract()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
