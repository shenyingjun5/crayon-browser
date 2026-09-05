#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "crayon/browser_engine/ids.h"
#include "crayon/browser_preferences/preference_store.h"
#include "crayon/browser_profiles_view/profile_picker.h"
#include "crayon/browser_settings_view/settings_page_state_machine.h"

namespace crayon::browser::cef_shell::window {

enum class AlloyProfileSettingsResult {
  kSuccess = 0,
  kUnknownProfile,
  kAlreadyActive,
  kBusy,
  kInvalidValue,
  kNotPersistent,
  kExternalFailure,
  kStaleGeneration,
  kInactive,
};

class AlloyProfileSettings final {
public:
  struct Callbacks final {
    std::function<bool(const browser_engine::ProfileId &,
                       browser_profiles_view::ProfileEntryKind)>
        switch_profile;
    std::function<bool(const browser_engine::ProfileId &, std::uint64_t)>
        open_incognito;
    std::function<bool(const browser_engine::ProfileId &,
                       const browser_preferences::PreferenceStore &)>
        save_preferences;
    std::function<bool(const browser_engine::ProfileId &, std::uint64_t)>
        begin_cleanup;
  };

  explicit AlloyProfileSettings(Callbacks callbacks);

  bool AddProfile(browser_engine::ProfileId profile_id,
                  std::string display_name,
                  browser_profiles_view::ProfileEntryKind kind,
                  browser_preferences::PreferenceStore preferences = {});
  AlloyProfileSettingsResult SwitchTo(const std::string &profile_id);
  AlloyProfileSettingsResult OpenIncognito();

  AlloyProfileSettingsResult
  ApplyPreference(const std::string &key,
                  browser_preferences::PreferenceValue value);
  bool RequestReset();
  AlloyProfileSettingsResult ConfirmReset();
  void CancelReset();

  std::optional<std::uint64_t> BeginCleanup();
  AlloyProfileSettingsResult CompleteCleanup(std::uint64_t generation,
                                             bool succeeded,
                                             std::string failure_token = {});
  void AcknowledgeCleanupFailure();
  bool Shutdown();

  const browser_preferences::PreferenceStore *
  PreferencesFor(const std::string &profile_id) const;
  const browser_profiles_view::ProfilePickerModel &picker() const noexcept {
    return picker_;
  }
  const browser_settings_view::SettingsPageStateMachine &
  settings() const noexcept {
    return settings_;
  }
  const std::string &last_failure_token() const noexcept {
    return last_failure_token_;
  }

private:
  struct ProfileRecord final {
    browser_engine::ProfileId id;
    browser_profiles_view::ProfileEntryKind kind;
    browser_preferences::PreferenceStore preferences;
  };

  ProfileRecord *ActiveRecord();
  const ProfileRecord *Find(const std::string &profile_id) const;
  std::optional<std::uint64_t> NextGeneration();

  Callbacks callbacks_;
  browser_profiles_view::ProfilePickerModel picker_;
  browser_settings_view::SettingsPageStateMachine settings_;
  std::map<std::string, ProfileRecord> profiles_;
  std::optional<std::uint64_t> pending_cleanup_generation_;
  std::string pending_cleanup_profile_;
  std::string last_failure_token_;
  std::uint64_t next_generation_ = 1;
  bool active_ = true;
};

} // namespace crayon::browser::cef_shell::window
