#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_navigation/navigation_controller.h"
#include "crayon/browser_navigation/site_identity.h"

namespace {

using crayon::browser_navigation::EvaluateSiteIdentity;
using crayon::browser_navigation::IsValid;
using crayon::browser_navigation::NavigationController;
using crayon::browser_navigation::SiteIdentity;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// ---------- SiteIdentity ----------

bool EmptyUrlIsUnknown() {
  CHECK(EvaluateSiteIdentity("") == SiteIdentity::kUnknown);
  return true;
}

bool HttpsIsSecure() {
  CHECK(EvaluateSiteIdentity("https://example.test") == SiteIdentity::kSecure);
  CHECK(EvaluateSiteIdentity("https://example.test:8443/path") ==
        SiteIdentity::kSecure);
  return true;
}

bool HttpIsInsecure() {
  CHECK(EvaluateSiteIdentity("http://example.test") == SiteIdentity::kInsecure);
  return true;
}

bool FileAndCrayonAreLocal() {
  CHECK(EvaluateSiteIdentity("file:///tmp/test.html") == SiteIdentity::kLocal);
  CHECK(EvaluateSiteIdentity("crayon://newtab/") == SiteIdentity::kLocal);
  CHECK(EvaluateSiteIdentity("about:blank") == SiteIdentity::kLocal);
  return true;
}

bool DangerousSchemesAreBlocked() {
  CHECK(EvaluateSiteIdentity("javascript:alert(1)") == SiteIdentity::kDangerous);
  CHECK(EvaluateSiteIdentity("data:text/html,<script>") ==
        SiteIdentity::kDangerous);
  CHECK(EvaluateSiteIdentity("vbscript:msgbox(1)") == SiteIdentity::kDangerous);
  return true;
}

bool UnknownSchemeIsUnknown() {
  CHECK(EvaluateSiteIdentity("ftp://example.test") == SiteIdentity::kUnknown);
  CHECK(EvaluateSiteIdentity("custom://app") == SiteIdentity::kUnknown);
  return true;
}

bool IsValidCoversAllIdentities() {
  CHECK(IsValid(SiteIdentity::kUnknown));
  CHECK(IsValid(SiteIdentity::kSecure));
  CHECK(IsValid(SiteIdentity::kInsecure));
  CHECK(IsValid(SiteIdentity::kLocal));
  CHECK(IsValid(SiteIdentity::kDangerous));
  return true;
}

// ---------- NavigationController ----------

bool NewTabHasNoNavigation() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  CHECK(ctrl.CurrentNavigationId("tab-1") == 0);
  CHECK(!ctrl.IsLoading("tab-1"));
  CHECK(!ctrl.CanGoBack("tab-1"));
  CHECK(!ctrl.CanGoForward("tab-1"));
  CHECK(!ctrl.CanReload("tab-1"));
  CHECK(!ctrl.CanStop("tab-1"));
  return true;
}

bool NavigationStartedSetsLoading() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);
  CHECK(ctrl.IsLoading("tab-1"));
  CHECK(ctrl.CurrentNavigationId("tab-1") == 1);
  return true;
}

bool NavigationCompletedClearsLoading() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);
  ctrl.SetCanGoBack("tab-1", true);
  ctrl.OnNavigationCompleted("tab-1", 1);
  CHECK(!ctrl.IsLoading("tab-1"));
  CHECK(ctrl.CanGoBack("tab-1"));
  CHECK(ctrl.CanReload("tab-1"));
  return true;
}

bool NavigationFailedAlsoClearsLoading() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);
  ctrl.OnNavigationFailed("tab-1", 1);
  CHECK(!ctrl.IsLoading("tab-1"));
  return true;
}

bool LateEventsAreIgnored() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 2);
  ctrl.SetCanGoBack("tab-1", true);
  // Event for old navigation ID 1 should be ignored
  ctrl.OnNavigationCompleted("tab-1", 1);
  CHECK(ctrl.IsLoading("tab-1"));  // still loading from nav 2
  return true;
}

bool CommandsRespectCapability() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);

  // Without history flags, back/forward unavailable
  CHECK(!ctrl.GoBack("tab-1"));
  CHECK(!ctrl.GoForward("tab-1"));

  // Reload available once navigation_id is set
  CHECK(ctrl.Reload("tab-1"));

  // Stop available while loading
  CHECK(ctrl.Stop("tab-1"));

  // After completed
  ctrl.SetCanGoBack("tab-1", true);
  ctrl.SetCanGoForward("tab-1", true);
  ctrl.OnNavigationCompleted("tab-1", 1);

  CHECK(ctrl.GoBack("tab-1"));
  CHECK(ctrl.GoForward("tab-1"));
  CHECK(!ctrl.Stop("tab-1"));  // no longer loading
  return true;
}

bool TabCloseRemovesState() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);
  ctrl.OnTabClosed("tab-1");
  CHECK(ctrl.FindTab("tab-1") == nullptr);
  CHECK(!ctrl.IsLoading("tab-1"));
  return true;
}

bool UnknownTabIsUnavailable() {
  NavigationController ctrl;
  CHECK(!ctrl.IsLoading("missing"));
  CHECK(!ctrl.CanGoBack("missing"));
  CHECK(!ctrl.GoBack("missing"));
  CHECK(!ctrl.Reload("missing"));
  return true;
}

bool ShutdownClearsAll() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);
  ctrl.Shutdown();
  CHECK(ctrl.FindTab("tab-1") == nullptr);
  CHECK(ctrl.TabCount() == 0);
  return true;
}

bool MultipleTabsIndependent() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-a");
  ctrl.OnTabCreated("tab-b");
  ctrl.OnNavigationStarted("tab-a", 1);
  ctrl.OnNavigationStarted("tab-b", 2);
  ctrl.SetCanGoBack("tab-a", true);
  ctrl.OnNavigationCompleted("tab-a", 1);
  CHECK(!ctrl.IsLoading("tab-a"));
  CHECK(ctrl.IsLoading("tab-b"));
  CHECK(ctrl.CanGoBack("tab-a"));
  CHECK(!ctrl.CanGoBack("tab-b"));
  return true;
}

bool NavigationIdZeroIsIgnored() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 0);
  CHECK(ctrl.CurrentNavigationId("tab-1") == 0);
  CHECK(!ctrl.IsLoading("tab-1"));
  return true;
}

bool DuplicateTabCreationIgnored() {
  NavigationController ctrl;
  ctrl.OnTabCreated("tab-1");
  ctrl.OnNavigationStarted("tab-1", 1);
  ctrl.OnTabCreated("tab-1");  // duplicate
  CHECK(ctrl.CurrentNavigationId("tab-1") == 1);  // original state preserved
  return true;
}

}  // namespace

int main() {
  if (!EmptyUrlIsUnknown() ||
      !HttpsIsSecure() ||
      !HttpIsInsecure() ||
      !FileAndCrayonAreLocal() ||
      !DangerousSchemesAreBlocked() ||
      !UnknownSchemeIsUnknown() ||
      !IsValidCoversAllIdentities() ||
      !NewTabHasNoNavigation() ||
      !NavigationStartedSetsLoading() ||
      !NavigationCompletedClearsLoading() ||
      !NavigationFailedAlsoClearsLoading() ||
      !LateEventsAreIgnored() ||
      !CommandsRespectCapability() ||
      !TabCloseRemovesState() ||
      !UnknownTabIsUnavailable() ||
      !ShutdownClearsAll() ||
      !MultipleTabsIndependent() ||
      !NavigationIdZeroIsIgnored() ||
      !DuplicateTabCreationIgnored()) {
    return 1;
  }
  return 0;
}
