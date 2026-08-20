#include "browser/permission/permission_store.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using crayon::browser::cef_shell::permission::PermissionDecision;
using crayon::browser::cef_shell::permission::PermissionKind;
using crayon::browser::cef_shell::permission::PermissionStore;

namespace {

bool TestPassed = true;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << std::endl;
    TestPassed = false;
  }
}

}  // namespace

int main() {
  PermissionStore store;
  const std::string kOrigin = "https://example.com";
  const std::string kOther = "https://other.com";

  // Default deny.
  Check(store.Query(kOrigin, PermissionKind::kCamera) ==
            PermissionDecision::kDeny,
        "default is deny for camera");
  Check(store.Query(kOrigin, PermissionKind::kMicrophone) ==
            PermissionDecision::kDeny,
        "default is deny for microphone");
  Check(store.Query(kOrigin, PermissionKind::kNotifications) ==
            PermissionDecision::kDeny,
        "default is deny for notifications");
  Check(store.Query(kOrigin, PermissionKind::kGeolocation) ==
            PermissionDecision::kDeny,
        "default is deny for geolocation");
  Check(store.Query(kOrigin, PermissionKind::kClipboardRead) ==
            PermissionDecision::kDeny,
        "default is deny for clipboard-read");
  Check(store.Query(kOrigin, PermissionKind::kClipboardWrite) ==
            PermissionDecision::kDeny,
        "default is deny for clipboard-write");
  Check(store.Query(kOrigin, PermissionKind::kDownload) ==
            PermissionDecision::kDeny,
        "default is deny for download");

  // Allow session.
  store.Record(kOrigin, PermissionKind::kCamera,
               PermissionDecision::kAllowSession);
  Check(store.Query(kOrigin, PermissionKind::kCamera) ==
            PermissionDecision::kAllowSession,
        "session allow recorded");
  // Other kinds still denied for same origin.
  Check(store.Query(kOrigin, PermissionKind::kMicrophone) ==
            PermissionDecision::kDeny,
        "other kind still denied after session allow");
  // Other origin still denied.
  Check(store.Query(kOther, PermissionKind::kCamera) ==
            PermissionDecision::kDeny,
        "other origin still denied");

  // Allow persistent.
  store.Record(kOrigin, PermissionKind::kMicrophone,
               PermissionDecision::kAllowPersistent);
  Check(store.Query(kOrigin, PermissionKind::kMicrophone) ==
            PermissionDecision::kAllowPersistent,
        "persistent allow recorded");

  // Explicit deny.
  store.Record(kOrigin, PermissionKind::kNotifications,
               PermissionDecision::kDeny);
  Check(store.Query(kOrigin, PermissionKind::kNotifications) ==
            PermissionDecision::kDeny,
        "explicit deny recorded");

  // Overwrite.
  store.Record(kOrigin, PermissionKind::kCamera,
               PermissionDecision::kAllowPersistent);
  Check(store.Query(kOrigin, PermissionKind::kCamera) ==
            PermissionDecision::kAllowPersistent,
        "overwrite session with persistent");

  // Clear session decisions.
  store.Record(kOther, PermissionKind::kCamera,
               PermissionDecision::kAllowSession);
  store.ClearSessionDecisions();
  Check(store.Query(kOrigin, PermissionKind::kCamera) ==
            PermissionDecision::kAllowPersistent,
        "persistent survives session clear");
  Check(store.Query(kOrigin, PermissionKind::kMicrophone) ==
            PermissionDecision::kAllowPersistent,
        "persistent microphone survives session clear");
  Check(store.Query(kOrigin, PermissionKind::kNotifications) ==
            PermissionDecision::kDeny,
        "explicit deny survives session clear");
  Check(store.Query(kOther, PermissionKind::kCamera) ==
            PermissionDecision::kDeny,
        "session decision cleared");

  // Clear for origin.
  store.ClearForOrigin(kOrigin);
  Check(store.Query(kOrigin, PermissionKind::kCamera) ==
            PermissionDecision::kDeny,
        "camera cleared for origin");
  Check(store.Query(kOrigin, PermissionKind::kMicrophone) ==
            PermissionDecision::kDeny,
        "microphone cleared for origin");
  Check(store.Query(kOther, PermissionKind::kCamera) ==
            PermissionDecision::kDeny,
        "other origin untouched");

  // Clear all.
  store.Record(kOrigin, PermissionKind::kGeolocation,
               PermissionDecision::kAllowPersistent);
  store.Record(kOther, PermissionKind::kDownload,
               PermissionDecision::kAllowSession);
  store.ClearAll();
  Check(store.Query(kOrigin, PermissionKind::kGeolocation) ==
            PermissionDecision::kDeny,
        "clear all removes persistent");
  Check(store.Query(kOther, PermissionKind::kDownload) ==
            PermissionDecision::kDeny,
        "clear all removes session");

  // Snapshot.
  store.Record("https://a.com", PermissionKind::kCamera,
               PermissionDecision::kAllowSession);
  store.Record("https://b.com", PermissionKind::kMicrophone,
               PermissionDecision::kAllowPersistent);
  const auto snapshot = store.Snapshot();
  Check(snapshot.size() == 2, "snapshot size is 2");

  if (TestPassed) {
    std::cout << "ALL TESTS PASSED" << std::endl;
    return 0;
  }
  return 1;
}
