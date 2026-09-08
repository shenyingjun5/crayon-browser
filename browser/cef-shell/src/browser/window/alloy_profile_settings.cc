#include "browser/window/alloy_profile_settings.h"

#include <limits>
#include <utility>

namespace crayon::browser::cef_shell::window {

namespace {

constexpr char kPreferenceSaveFailure[] = "preferences-save-failed";
constexpr char kCleanupStartFailure[] = "cleanup-start-failed";
constexpr char kCleanupFailure[] = "cleanup-failed";

class ScopedCallback final {
 public:
  explicit ScopedCallback(bool *in_progress) : in_progress_(in_progress) {
    *in_progress_ = true;
  }
  ~ScopedCallback() { *in_progress_ = false; }
  ScopedCallback(const ScopedCallback &) = delete;
  ScopedCallback &operator=(const ScopedCallback &) = delete;

 private:
  bool *in_progress_;
};

} // namespace

AlloyProfileSettings::AlloyProfileSettings(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

bool AlloyProfileSettings::AddProfile(
    browser_engine::ProfileId profile_id, std::string display_name,
    browser_profiles_view::ProfileEntryKind kind,
    browser_preferences::PreferenceStore preferences) {
  const std::string key = profile_id.value();
  if (!active_ || callback_in_progress_ || profiles_.count(key) != 0 ||
      !picker_.AddProfile(key, display_name, kind)) {
    return false;
  }
  profiles_.emplace(
      key, ProfileRecord{std::move(profile_id), kind, std::move(preferences)});
  return true;
}

AlloyProfileSettingsResult
AlloyProfileSettings::SwitchTo(const std::string &profile_id) {
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (callback_in_progress_)
    return AlloyProfileSettingsResult::kBusy;
  const ProfileRecord *record = Find(profile_id);
  if (!record)
    return AlloyProfileSettingsResult::kUnknownProfile;
  if (picker_.cleanup_failure_pending()) {
    return AlloyProfileSettingsResult::kBusy;
  }
  if (picker_.active_profile() == profile_id) {
    return AlloyProfileSettingsResult::kAlreadyActive;
  }
  if (!callbacks_.switch_profile) {
    last_failure_token_ = "profile-switch-failed";
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  const auto callback = callbacks_.switch_profile;
  const auto id = record->id;
  const auto kind = record->kind;
  bool accepted = false;
  {
    ScopedCallback call(&callback_in_progress_);
    accepted = callback(id, kind);
  }
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (!accepted || !Find(id.value())) {
    last_failure_token_ = "profile-switch-failed";
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  if (picker_.SwitchTo(id.value()) !=
      browser_profiles_view::SwitchOutcome::kSwitched) {
    last_failure_token_ = "profile-switch-state-failed";
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  last_failure_token_.clear();
  return AlloyProfileSettingsResult::kSuccess;
}

AlloyProfileSettingsResult AlloyProfileSettings::OpenIncognito() {
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (callback_in_progress_)
    return AlloyProfileSettingsResult::kBusy;
  ProfileRecord *record = ActiveRecord();
  const auto generation = NextGeneration();
  if (!record || !generation || !picker_.RequestIncognitoWindow() ||
      !callbacks_.open_incognito) {
    last_failure_token_ = "incognito-open-failed";
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  const auto callback = callbacks_.open_incognito;
  const auto id = record->id;
  bool accepted = false;
  {
    ScopedCallback call(&callback_in_progress_);
    accepted = callback(id, *generation);
  }
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (!accepted || !Find(id.value())) {
    last_failure_token_ = "incognito-open-failed";
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  last_failure_token_.clear();
  return AlloyProfileSettingsResult::kSuccess;
}

AlloyProfileSettingsResult AlloyProfileSettings::ApplyPreference(
    const std::string &key, browser_preferences::PreferenceValue value) {
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (callback_in_progress_)
    return AlloyProfileSettingsResult::kBusy;
  ProfileRecord *record = ActiveRecord();
  if (!record)
    return AlloyProfileSettingsResult::kUnknownProfile;
  if (record->kind != browser_profiles_view::ProfileEntryKind::kRegular) {
    return AlloyProfileSettingsResult::kNotPersistent;
  }
  auto candidate = record->preferences;
  if (!candidate.Set(key, std::move(value))) {
    return AlloyProfileSettingsResult::kInvalidValue;
  }
  settings_.MarkDirty();
  if (!callbacks_.save_preferences) {
    settings_.ClearDirty();
    last_failure_token_ = kPreferenceSaveFailure;
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  const auto callback = callbacks_.save_preferences;
  const auto id = record->id;
  bool accepted = false;
  {
    ScopedCallback call(&callback_in_progress_);
    accepted = callback(id, candidate);
  }
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  record = ActiveRecord();
  if (!accepted || !record || record->id.value() != id.value()) {
    settings_.ClearDirty();
    last_failure_token_ = kPreferenceSaveFailure;
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  record->preferences = std::move(candidate);
  settings_.ClearDirty();
  last_failure_token_.clear();
  return AlloyProfileSettingsResult::kSuccess;
}

bool AlloyProfileSettings::RequestReset() {
  return active_ && !callback_in_progress_ && ActiveRecord() &&
         settings_.RequestReset();
}

AlloyProfileSettingsResult AlloyProfileSettings::ConfirmReset() {
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (callback_in_progress_)
    return AlloyProfileSettingsResult::kBusy;
  ProfileRecord *record = ActiveRecord();
  if (!record || !settings_.reset_pending()) {
    return AlloyProfileSettingsResult::kInvalidValue;
  }
  if (record->kind != browser_profiles_view::ProfileEntryKind::kRegular) {
    return AlloyProfileSettingsResult::kNotPersistent;
  }
  auto candidate = record->preferences;
  candidate.ResetAll();
  if (!callbacks_.save_preferences) {
    last_failure_token_ = kPreferenceSaveFailure;
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  const auto callback = callbacks_.save_preferences;
  const auto id = record->id;
  bool accepted = false;
  {
    ScopedCallback call(&callback_in_progress_);
    accepted = callback(id, candidate);
  }
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  record = ActiveRecord();
  if (!accepted || !record || record->id.value() != id.value()) {
    last_failure_token_ = kPreferenceSaveFailure;
    return AlloyProfileSettingsResult::kExternalFailure;
  }
  if (!settings_.ConfirmReset()) {
    return AlloyProfileSettingsResult::kInvalidValue;
  }
  record->preferences = std::move(candidate);
  last_failure_token_.clear();
  return AlloyProfileSettingsResult::kSuccess;
}

void AlloyProfileSettings::CancelReset() {
  if (active_ && !callback_in_progress_)
    settings_.CancelReset();
}

std::optional<std::uint64_t> AlloyProfileSettings::BeginCleanup() {
  if (!active_ || callback_in_progress_ || pending_cleanup_generation_ ||
      picker_.cleanup_failure_pending()) {
    return std::nullopt;
  }
  ProfileRecord *record = ActiveRecord();
  const auto generation = NextGeneration();
  if (!record || !generation || !callbacks_.begin_cleanup) {
    last_failure_token_ = kCleanupStartFailure;
    if (record) {
      static_cast<void>(picker_.ReportCleanupFailure(record->id.value(),
                                                     kCleanupStartFailure));
    }
    return std::nullopt;
  }
  pending_cleanup_generation_ = generation;
  pending_cleanup_profile_ = record->id.value();
  const auto callback = callbacks_.begin_cleanup;
  const auto id = record->id;
  bool accepted = false;
  {
    ScopedCallback call(&callback_in_progress_);
    accepted = callback(id, *generation);
  }
  if (!active_)
    return std::nullopt;
  if (!accepted) {
    if (pending_cleanup_generation_ && *pending_cleanup_generation_ == *generation) {
      pending_cleanup_generation_.reset();
      pending_cleanup_profile_.clear();
      static_cast<void>(picker_.ReportCleanupFailure(id.value(), kCleanupStartFailure));
      last_failure_token_ = kCleanupStartFailure;
    }
    return std::nullopt;
  }
  if (pending_cleanup_generation_ && *pending_cleanup_generation_ == *generation)
    last_failure_token_.clear();
  return generation;
}

AlloyProfileSettingsResult
AlloyProfileSettings::CompleteCleanup(std::uint64_t generation, bool succeeded,
                                      std::string failure_token) {
  if (!active_)
    return AlloyProfileSettingsResult::kInactive;
  if (!pending_cleanup_generation_ ||
      *pending_cleanup_generation_ != generation) {
    return AlloyProfileSettingsResult::kStaleGeneration;
  }
  const std::string profile = std::move(pending_cleanup_profile_);
  pending_cleanup_generation_.reset();
  pending_cleanup_profile_.clear();
  if (succeeded) {
    last_failure_token_.clear();
    return AlloyProfileSettingsResult::kSuccess;
  }
  if (!browser_profiles_view::ProfilePickerModel::IsValidToken(failure_token)) {
    failure_token = kCleanupFailure;
  }
  static_cast<void>(picker_.ReportCleanupFailure(profile, failure_token));
  last_failure_token_ = std::move(failure_token);
  return AlloyProfileSettingsResult::kExternalFailure;
}

void AlloyProfileSettings::AcknowledgeCleanupFailure() {
  if (!active_ || callback_in_progress_)
    return;
  picker_.AcknowledgeCleanupFailure();
  last_failure_token_.clear();
}

bool AlloyProfileSettings::Shutdown() {
  if (!active_)
    return true;
  active_ = false;
  callbacks_ = {};
  pending_cleanup_generation_.reset();
  pending_cleanup_profile_.clear();
  profiles_.clear();
  settings_.Shutdown();
  picker_.Close();
  return true;
}

const browser_preferences::PreferenceStore *
AlloyProfileSettings::PreferencesFor(const std::string &profile_id) const {
  const auto found = profiles_.find(profile_id);
  return active_ && found != profiles_.end() ? &found->second.preferences
                                             : nullptr;
}

AlloyProfileSettings::ProfileRecord *AlloyProfileSettings::ActiveRecord() {
  const auto found = profiles_.find(picker_.active_profile());
  return found == profiles_.end() ? nullptr : &found->second;
}

const AlloyProfileSettings::ProfileRecord *
AlloyProfileSettings::Find(const std::string &profile_id) const {
  const auto found = profiles_.find(profile_id);
  return found == profiles_.end() ? nullptr : &found->second;
}

std::optional<std::uint64_t> AlloyProfileSettings::NextGeneration() {
  if (next_generation_ == 0)
    return std::nullopt;
  const std::uint64_t generation = next_generation_;
  next_generation_ = generation == (std::numeric_limits<std::uint64_t>::max)()
                         ? 0
                         : generation + 1;
  return generation;
}

} // namespace crayon::browser::cef_shell::window
