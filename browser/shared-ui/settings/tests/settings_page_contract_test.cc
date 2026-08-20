#include <cstdlib>
#include <iostream>

#include "crayon/browser_settings_view/settings_page_state_machine.h"

namespace {

using crayon::browser_settings_view::IsValid;
using crayon::browser_settings_view::SettingsPageStateMachine;
using crayon::browser_settings_view::SettingsSection;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool SectionNavigation() {
  SettingsPageStateMachine page;
  CHECK(page.current_section() == SettingsSection::kStartup);
  CHECK(page.OpenSection(SettingsSection::kPrivacy));
  CHECK(page.current_section() == SettingsSection::kPrivacy);
  CHECK(!page.OpenSection(static_cast<SettingsSection>(42)));
  CHECK(page.current_section() == SettingsSection::kPrivacy);
  CHECK(IsValid(SettingsSection::kDownloads));
  CHECK(!IsValid(static_cast<SettingsSection>(9)));
  return true;
}

bool DirtyTracking() {
  SettingsPageStateMachine page;
  CHECK(!page.dirty());
  page.MarkDirty();
  CHECK(page.dirty());
  page.ClearDirty();
  CHECK(!page.dirty());
  return true;
}

bool ResetRequiresConfirmation() {
  SettingsPageStateMachine page;
  CHECK(!page.ConfirmReset());  // nothing pending
  CHECK(page.RequestReset());
  CHECK(page.reset_pending());
  page.CancelReset();
  CHECK(!page.reset_pending());
  CHECK(page.RequestReset());
  page.MarkDirty();
  CHECK(page.ConfirmReset());
  CHECK(!page.dirty());         // reset clears unsaved edits
  CHECK(!page.reset_pending());
  return true;
}

bool ShutdownRejectsEverything() {
  SettingsPageStateMachine page;
  page.MarkDirty();
  page.Shutdown();
  CHECK(!page.active());
  CHECK(!page.dirty());
  CHECK(!page.OpenSection(SettingsSection::kSearch));
  CHECK(!page.RequestReset());
  page.MarkDirty();
  CHECK(!page.dirty());
  return true;
}

}  // namespace

int main() {
  if (!SectionNavigation() || !DirtyTracking() ||
      !ResetRequiresConfirmation() || !ShutdownRejectsEverything()) {
    return 1;
  }
  return 0;
}
