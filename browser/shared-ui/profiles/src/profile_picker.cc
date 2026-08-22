#include "crayon/browser_profiles_view/profile_picker.h"

#include <algorithm>

namespace crayon::browser_profiles_view {
namespace {

bool IsIdCharset(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.';
}

bool IsDisplayNameCharset(char c) { return IsIdCharset(c) || c == ' '; }

}  // namespace

bool ProfilePickerModel::IsValidToken(const std::string& value) {
  return !value.empty() && value.size() <= kMaxProfileFieldLen &&
         std::all_of(value.begin(), value.end(), IsIdCharset);
}

bool IsValidDisplayName(const std::string& value) {
  return !value.empty() && value.size() <= kMaxProfileFieldLen &&
         std::all_of(value.begin(), value.end(), IsDisplayNameCharset);
}

bool ProfilePickerModel::AddProfile(const std::string& id,
                                    const std::string& display_name,
                                    ProfileEntryKind kind) {
  if (!IsValidToken(id) || !IsValidDisplayName(display_name)) {
    return false;
  }
  if (entries_.size() >= kMaxProfiles || Find(id) != nullptr) {
    return false;
  }
  entries_.push_back(ProfileEntry{id, display_name, kind});
  if (active_.empty()) {
    active_ = id;
  }
  return true;
}

bool ProfilePickerModel::Open() noexcept {
  if (state_ == PickerState::kOpen) {
    return false;
  }
  state_ = PickerState::kOpen;
  return true;
}

void ProfilePickerModel::Close() noexcept {
  state_ = PickerState::kClosed;
}

const ProfileEntry* ProfilePickerModel::Find(const std::string& id) const {
  for (const ProfileEntry& entry : entries_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

SwitchOutcome ProfilePickerModel::SwitchTo(const std::string& id) {
  if (cleanup_failure_pending_) {
    return SwitchOutcome::kBusy;
  }
  if (Find(id) == nullptr) {
    return SwitchOutcome::kUnknownProfile;
  }
  if (id == active_) {
    return SwitchOutcome::kAlreadyActive;
  }
  active_ = id;
  return SwitchOutcome::kSwitched;
}

bool ProfilePickerModel::RequestIncognitoWindow() {
  if (active_.empty()) {
    return false;
  }
  // The view model only counts the request; the window itself is
  // ephemeral and deliberately never enters session restore (the
  // coordinator refuses incognito recordings).
  return true;
}

bool ProfilePickerModel::ReportCleanupFailure(const std::string& id,
                                              const std::string& detail_token) {
  if (Find(id) == nullptr || !IsValidToken(detail_token)) {
    return false;
  }
  cleanup_failure_pending_ = true;
  cleanup_profile_ = id;
  return true;
}

void ProfilePickerModel::AcknowledgeCleanupFailure() noexcept {
  cleanup_failure_pending_ = false;
  cleanup_profile_.clear();
}

}  // namespace crayon::browser_profiles_view
