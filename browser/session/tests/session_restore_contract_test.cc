// UX-014 contract tests: session restore orchestration (policy-driven
// restore, incognito never restored, crash tail drop, stale epochs,
// per-profile isolation).
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_session/session_restore.h"
#include "crayon/browser_session/session_snapshot.h"

namespace {

using crayon::browser_session::RecordedWindow;
using crayon::browser_session::RestoreDecision;
using crayon::browser_session::SessionRestoreCoordinator;
using crayon::browser_session::StartupPolicy;
using crayon::browser_session::WindowKind;
using crayon::browser_session::DecodeSessionSnapshot;
using crayon::browser_session::EncodeSessionSnapshotV2;
using crayon::browser_session::SessionProfileSnapshot;
using crayon::browser_session::SessionSnapshotError;
using crayon::browser_session::SessionTabSnapshot;
using crayon::browser_session::SessionWindowSnapshot;

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

bool SnapshotV2RoundTrip() {
  SessionProfileSnapshot profile{
      "p1",
      {SessionWindowSnapshot{
          "w1",
          {SessionTabSnapshot{"https://example.test/a", true, false,
                              std::string("group-a")},
           SessionTabSnapshot{"crayon://mdv/app.html", false, true,
                              std::nullopt}},
          1}}};
  SessionSnapshotError error = SessionSnapshotError::kMalformed;
  const auto encoded = EncodeSessionSnapshotV2(profile, &error);
  CHECK(encoded.has_value());
  CHECK(error == SessionSnapshotError::kNone);
  const auto decoded = DecodeSessionSnapshot(*encoded, &error);
  CHECK(decoded.has_value());
  CHECK(error == SessionSnapshotError::kNone);
  CHECK(decoded->profile_id == "p1" && decoded->windows.size() == 1);
  const auto& window = decoded->windows.front();
  CHECK(window.window_id == "w1" && window.active_index == 1 &&
        window.tabs.size() == 2);
  CHECK(window.tabs[0].pinned && !window.tabs[0].muted &&
        window.tabs[0].group == std::optional<std::string>("group-a"));
  CHECK(window.tabs[1].url == "crayon://mdv/app.html" &&
        window.tabs[1].muted);
  return true;
}

bool SnapshotV1CompatibilityAndCorruption() {
  SessionSnapshotError error = SessionSnapshotError::kNone;
  const auto legacy = DecodeSessionSnapshot(
      "CRAYON_SESSION_V1\r\nP\t7031\r\nW\t7731\t2\r\n", &error);
  CHECK(legacy.has_value());
  CHECK(legacy->windows.size() == 1 &&
        legacy->windows[0].tabs.size() == 2);
  CHECK(legacy->windows[0].tabs[0].url == "crayon://newtab/");
  CHECK(!DecodeSessionSnapshot("CRAYON_SESSION_V9\nP\t7031\n", &error));
  CHECK(error == SessionSnapshotError::kUnsupportedVersion);
  CHECK(!DecodeSessionSnapshot(
      "CRAYON_SESSION_V1\nP\t7031\nW\t7731\t1\nW\t7731\t1\n",
      &error));
  CHECK(error == SessionSnapshotError::kDuplicateIdentity);
  CHECK(!DecodeSessionSnapshot(
      "CRAYON_SESSION_V2\nP\t7031\nW\t7731\t0\t1\n"
      "T\t68747470733a2f2f757365723a70617373406578616d706c652e746573742f"
      "\t0\t0\t\n",
      &error));
  CHECK(error == SessionSnapshotError::kInvalidValue);
  SessionWindowSnapshot too_many_groups{"w1", {}, 0};
  for (std::size_t index = 0;
       index <= crayon::browser_session::kMaxSessionGroupsPerWindow; ++index) {
    too_many_groups.tabs.push_back(
        {"https://example.test/" + std::to_string(index), false, false,
         "group-" + std::to_string(index)});
  }
  CHECK(!crayon::browser_session::IsValid(too_many_groups));
  return true;
}

}  // namespace

int main() {
  const bool ok = IdValidation() && IncognitoNeverRecorded() && PolicyDrivenRestore() &&
                  CrashRecoveryDropsTail() && StaleEpochRejected() && CrossProfileIsolation() &&
                  BoundedStores() && SnapshotV2RoundTrip() &&
                  SnapshotV1CompatibilityAndCorruption();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "session_restore_contract_test passed\n";
  return EXIT_SUCCESS;
}
