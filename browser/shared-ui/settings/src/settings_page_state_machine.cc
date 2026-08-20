#include "crayon/browser_settings_view/settings_page_state_machine.h"

namespace crayon::browser_settings_view {

bool SettingsPageStateMachine::OpenSection(SettingsSection section) noexcept {
  if (!active_ || !IsValid(section)) {
    return false;
  }
  current_ = section;
  return true;
}

void SettingsPageStateMachine::MarkDirty() noexcept {
  if (active_) {
    dirty_ = true;
  }
}

void SettingsPageStateMachine::ClearDirty() noexcept {
  dirty_ = false;
}

bool SettingsPageStateMachine::RequestReset() noexcept {
  if (!active_) {
    return false;
  }
  reset_pending_ = true;
  return true;
}

bool SettingsPageStateMachine::ConfirmReset() noexcept {
  if (!active_ || !reset_pending_) {
    return false;
  }
  reset_pending_ = false;
  dirty_ = false;
  return true;
}

void SettingsPageStateMachine::CancelReset() noexcept {
  reset_pending_ = false;
}

void SettingsPageStateMachine::Shutdown() noexcept {
  active_ = false;
  dirty_ = false;
  reset_pending_ = false;
}

}  // namespace crayon::browser_settings_view
