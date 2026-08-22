// UX-014 contract tests: session restore orchestration (policy-driven
// restore, incognito never restored, crash tail drop, stale epochs,
// per-profile isolation).
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_session/session_restore.h"

namespace {

using crayon::browser_session::RecordedWindow;
using crayon::browser_session::RestoreDecision;
using crayon::browser_session::SessionRestoreCoordinator;
using crayon::browser_session::StartupPolicy;
using crayon::browser_session::WindowKind;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool IdValidation() {
  CHECK(SessionRestoreCoordinator::IsValidId("profile.a-1"));
  CHECK(!SessionRestoreCoordinator::IsValidId(""));
  CHECK(!SessionRestoreCoordinator::IsValidId("has space"));
  CHECK(!SessionRestoreCoordinator::IsValidId(std::string(200, 'a')));
  return true;
}

bool IncognitoNeverRecorded() {
  SessionRestoreCoordinator coordinator;
  CHECK(!coordinator.RecordWindow("p1", "w1", 3, WindowKind::kIncognito));
  CHECK(coordinator.recorded_window_count("p1") == 0);
  CHECK(coordinator.RecordWindow("p1", "w1", 3, WindowKind::kRegular));
  CHECK(coordinator.recorded_window_count("p1") == 1);
  // Invalid inputs fail closed.
  CHECK(!coordinator.RecordWindow("p1", "w2", 0, WindowKind::kRegular));
  CHECK(!coordinator.RecordWindow("p1", "w3", 65, WindowKind::kRegular));
  CHECK(!coordinator.RecordWindow("bad id", "w4", 1, WindowKind::kRegular));
  return true;
}

bool PolicyDrivenRestore() {
  SessionRestoreCoordinator coordinator;
  coordinator.RecordWindow("p1", "w1", 2, WindowKind::kRegular);
  coordinator.Checkpoint("p1");
  CHECK(coordinator.PlanRestore("p1", StartupPolicy::kNewTab) ==
        RestoreDecision::kNewTabOnly);
  CHECK(coordinator.PlanRestore("p1", StartupPolicy::kRestore) ==
        RestoreDecision::kRestoreRecorded);
  CHECK(coordinator.PlanRestore("unknown", StartupPolicy::kRestore) ==
        RestoreDecision::kNewTabOnly);
  return true;
}

bool CrashRecoveryDropsTail() {
  SessionRestoreCoordinator coordinator;
  coordinator.RecordWindow("p1", "w1", 2, WindowKind::kRegular);
  coordinator.Checkpoint("p1");
  // Recorded after the last checkpoint; lost in the crash.
  coordinator.RecordWindow("p1", "w2", 1, WindowKind::kRegular);
  coordinator.MarkCrashedLastExit("p1");
  CHECK(coordinator.PlanRestore("p1", StartupPolicy::kRestore) ==
        RestoreDecision::kRestoreAfterCrash);
  std::size_t dropped = 99;
  const std::vector<RecordedWindow> restored = coordinator.RestorableWindows("p1", true, &dropped);
  CHECK(restored.size() == 1);
  CHECK(restored[0].window_id == "w1");
  CHECK(dropped == 1);
  // Clean exit restores everything.
  coordinator.Checkpoint("p1");
  const std::vector<RecordedWindow> all = coordinator.RestorableWindows("p1", false, &dropped);
  CHECK(all.size() == 2);
  CHECK(dropped == 0);
  return true;
}

bool StaleEpochRejected() {
  SessionRestoreCoordinator coordinator;
  const std::uint64_t first = coordinator.AdvanceEpoch("p1");
  CHECK(first == 1);
  CHECK(coordinator.IsCurrentEpoch("p1", first));
  const std::uint64_t second = coordinator.AdvanceEpoch("p1");
  CHECK(!coordinator.IsCurrentEpoch("p1", first));   // old session rejected
  CHECK(coordinator.IsCurrentEpoch("p1", second));
  return true;
}

bool CrossProfileIsolation() {
  SessionRestoreCoordinator coordinator;
  coordinator.RecordWindow("p1", "w1", 2, WindowKind::kRegular);
  coordinator.Checkpoint("p1");
  CHECK(coordinator.recorded_window_count("p2") == 0);
  CHECK(coordinator.PlanRestore("p2", StartupPolicy::kRestore) ==
        RestoreDecision::kNewTabOnly);
  std::size_t dropped = 0;
  CHECK(coordinator.RestorableWindows("p2", false, &dropped).empty());
  CHECK(coordinator.ClearProfile("p1") == 1);
  CHECK(coordinator.ClearProfile("p1") == 0);
  CHECK(coordinator.recorded_window_count("p1") == 0);
  return true;
}

bool BoundedStores() {
  SessionRestoreCoordinator coordinator;
  for (std::size_t i = 0; i < crayon::browser_session::kMaxWindowsPerProfile; ++i) {
    CHECK(coordinator.RecordWindow("p1", "w" + std::to_string(i), 1, WindowKind::kRegular));
  }
  CHECK(!coordinator.RecordWindow("p1", "overflow", 1, WindowKind::kRegular));
  for (std::size_t i = 0; i < crayon::browser_session::kMaxProfiles - 1; ++i) {
    CHECK(coordinator.RecordWindow("profile-" + std::to_string(i), "w", 1, WindowKind::kRegular));
  }
  CHECK(!coordinator.RecordWindow("one-too-many", "w", 1, WindowKind::kRegular));
  return true;
}

}  // namespace

int main() {
  const bool ok = IdValidation() && IncognitoNeverRecorded() && PolicyDrivenRestore() &&
                  CrashRecoveryDropsTail() && StaleEpochRejected() && CrossProfileIsolation() &&
                  BoundedStores();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "session_restore_contract_test passed\n";
  return EXIT_SUCCESS;
}
