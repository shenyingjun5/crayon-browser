// UX-014 contract tests: profile picker view model (switching, incognito
// window requests, explicit cleanup-failure reporting, bounded lists).
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_profiles_view/profile_picker.h"

namespace {

using crayon::browser_profiles_view::PickerState;
using crayon::browser_profiles_view::ProfileEntryKind;
using crayon::browser_profiles_view::ProfilePickerModel;
using crayon::browser_profiles_view::SwitchOutcome;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool ProfileListManagement() {
  ProfilePickerModel picker;
  CHECK(picker.AddProfile("work", "Work", ProfileEntryKind::kRegular));
  CHECK(picker.AddProfile("personal", "Personal Space", ProfileEntryKind::kRegular));
  CHECK(!picker.AddProfile("work", "Duplicate", ProfileEntryKind::kRegular));
  CHECK(!picker.AddProfile("bad id", "X", ProfileEntryKind::kRegular));
  CHECK(!picker.AddProfile("x", "bad\nname", ProfileEntryKind::kRegular));
  CHECK(!picker.AddProfile("", "X", ProfileEntryKind::kRegular));
  CHECK(picker.entries().size() == 2);
  CHECK(picker.active_profile() == "work");  // first added becomes active
  return true;
}

bool PickerOpenClose() {
  ProfilePickerModel picker;
  picker.AddProfile("work", "Work", ProfileEntryKind::kRegular);
  CHECK(picker.state() == PickerState::kClosed);
  CHECK(picker.Open());
  CHECK(!picker.Open());  // already open
  CHECK(picker.state() == PickerState::kOpen);
  picker.Close();
  CHECK(picker.state() == PickerState::kClosed);
  return true;
}

bool SwitchMatrix() {
  ProfilePickerModel picker;
  picker.AddProfile("work", "Work", ProfileEntryKind::kRegular);
  picker.AddProfile("personal", "Personal", ProfileEntryKind::kRegular);
  picker.AddProfile("guest", "Guest", ProfileEntryKind::kGuest);
  CHECK(picker.SwitchTo("work") == SwitchOutcome::kAlreadyActive);
  CHECK(picker.SwitchTo("nope") == SwitchOutcome::kUnknownProfile);
  CHECK(picker.SwitchTo("personal") == SwitchOutcome::kSwitched);
  CHECK(picker.active_profile() == "personal");
  CHECK(picker.SwitchTo("guest") == SwitchOutcome::kSwitched);
  return true;
}

bool IncognitoRequest() {
  ProfilePickerModel picker;
  CHECK(!picker.RequestIncognitoWindow());  // no active profile
  picker.AddProfile("work", "Work", ProfileEntryKind::kRegular);
  CHECK(picker.RequestIncognitoWindow());
  // Incognito windows are ephemeral: nothing here feeds the session
  // restore coordinator (see session_restore_contract_test).
  return true;
}

bool CleanupFailureIsExplicit() {
  ProfilePickerModel picker;
  picker.AddProfile("work", "Work", ProfileEntryKind::kRegular);
  picker.AddProfile("personal", "Personal", ProfileEntryKind::kRegular);
  CHECK(!picker.ReportCleanupFailure("unknown", "cookie-store-locked"));
  CHECK(picker.ReportCleanupFailure("work", "cookie-store-locked"));
  CHECK(picker.cleanup_failure_pending());
  CHECK(picker.cleanup_failure_profile() == "work");
  // Switching is blocked until the failure is acknowledged.
  CHECK(picker.SwitchTo("personal") == SwitchOutcome::kBusy);
  picker.AcknowledgeCleanupFailure();
  CHECK(!picker.cleanup_failure_pending());
  CHECK(picker.cleanup_failure_profile().empty());
  CHECK(picker.SwitchTo("personal") == SwitchOutcome::kSwitched);
  return true;
}

}  // namespace

int main() {
  const bool ok = ProfileListManagement() && PickerOpenClose() && SwitchMatrix() &&
                  IncognitoRequest() && CleanupFailureIsExplicit();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "profile_picker_contract_test passed\n";
  return EXIT_SUCCESS;
}
