#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "browser/window/alloy_profile_settings.h"

namespace {

using crayon::browser::cef_shell::window::AlloyProfileSettings;
using crayon::browser::cef_shell::window::AlloyProfileSettingsResult;
using crayon::browser_engine::ProfileId;
using crayon::browser_preferences::PreferenceStore;
using crayon::browser_preferences::PreferenceValue;
using crayon::browser_profiles_view::ProfileEntryKind;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

ProfileId Profile(const std::string &value) {
  return *ProfileId::TryCreate(value);
}

bool ProfileSettingsContract() {
  bool allow_save = true;
  std::map<std::string, PreferenceStore> saved;
  std::string switched;
  std::uint64_t incognito_generation = 0;
  std::uint64_t cleanup_generation = 0;
  AlloyProfileSettings settings(
      {[&](const ProfileId &profile, ProfileEntryKind) {
         switched = profile.value();
         return true;
       },
       [&](const ProfileId &profile, std::uint64_t generation) {
         incognito_generation = generation;
         return profile.value() == "profile-a";
       },
       [&](const ProfileId &profile, const PreferenceStore &preferences) {
         if (!allow_save)
           return false;
         saved.insert_or_assign(profile.value(), preferences);
         return true;
       },
       [&](const ProfileId &profile, std::uint64_t generation) {
         cleanup_generation = generation;
         return profile.value() == "profile-a";
       }});
  CHECK(settings.AddProfile(Profile("profile-a"), "Profile One",
                            ProfileEntryKind::kRegular));
  CHECK(settings.AddProfile(Profile("profile-b"), "Guest",
                            ProfileEntryKind::kGuest));
  CHECK(!settings.AddProfile(Profile("profile-a"), "Duplicate",
                             ProfileEntryKind::kRegular));
  CHECK(settings.SwitchTo("missing") ==
        AlloyProfileSettingsResult::kUnknownProfile);
  CHECK(settings.SwitchTo("profile-a") ==
        AlloyProfileSettingsResult::kAlreadyActive);

  CHECK(
      settings.ApplyPreference(PreferenceStore::kTheme,
                               PreferenceValue{PreferenceStore::kThemeDark}) ==
      AlloyProfileSettingsResult::kSuccess);
  CHECK(
      std::get<std::int64_t>(
          settings.PreferencesFor("profile-a")->Get(PreferenceStore::kTheme)) ==
      PreferenceStore::kThemeDark);
  allow_save = false;
  CHECK(
      settings.ApplyPreference(PreferenceStore::kTheme,
                               PreferenceValue{PreferenceStore::kThemeLight}) ==
      AlloyProfileSettingsResult::kExternalFailure);
  CHECK(!settings.settings().dirty());
  CHECK(
      std::get<std::int64_t>(
          settings.PreferencesFor("profile-a")->Get(PreferenceStore::kTheme)) ==
      PreferenceStore::kThemeDark);
  CHECK(settings.ApplyPreference("unknown-key", PreferenceValue{true}) ==
        AlloyProfileSettingsResult::kInvalidValue);
  CHECK(!settings.settings().dirty());
  allow_save = true;
  CHECK(settings.RequestReset());
  allow_save = false;
  CHECK(settings.ConfirmReset() ==
        AlloyProfileSettingsResult::kExternalFailure);
  CHECK(settings.settings().reset_pending());
  allow_save = true;
  CHECK(settings.ConfirmReset() == AlloyProfileSettingsResult::kSuccess);
  CHECK(!settings.settings().dirty() && !settings.settings().reset_pending());

  CHECK(settings.OpenIncognito() == AlloyProfileSettingsResult::kSuccess);
  CHECK(incognito_generation != 0);
  const auto cleanup = settings.BeginCleanup();
  CHECK(cleanup && *cleanup == cleanup_generation);
  CHECK(settings.CompleteCleanup(*cleanup + 1, true) ==
        AlloyProfileSettingsResult::kStaleGeneration);
  CHECK(settings.CompleteCleanup(*cleanup, false, "disk-busy") ==
        AlloyProfileSettingsResult::kExternalFailure);
  CHECK(settings.picker().cleanup_failure_pending());
  CHECK(settings.SwitchTo("profile-b") == AlloyProfileSettingsResult::kBusy);
  settings.AcknowledgeCleanupFailure();
  CHECK(settings.SwitchTo("profile-b") == AlloyProfileSettingsResult::kSuccess);
  CHECK(switched == "profile-b");
  CHECK(
      settings.ApplyPreference(PreferenceStore::kTheme,
                               PreferenceValue{PreferenceStore::kThemeDark}) ==
      AlloyProfileSettingsResult::kNotPersistent);
  CHECK(settings.OpenIncognito() ==
        AlloyProfileSettingsResult::kExternalFailure);
  CHECK(settings.SwitchTo("profile-a") == AlloyProfileSettingsResult::kSuccess);
  const auto cleanup_ok = settings.BeginCleanup();
  CHECK(cleanup_ok && settings.CompleteCleanup(*cleanup_ok, true) ==
                          AlloyProfileSettingsResult::kSuccess);
  CHECK(settings.Shutdown() && settings.Shutdown());
  CHECK(settings.SwitchTo("profile-b") ==
        AlloyProfileSettingsResult::kInactive);
  CHECK(settings.PreferencesFor("profile-a") == nullptr);
  return true;
}

} // namespace

int main() { return ProfileSettingsContract() ? EXIT_SUCCESS : EXIT_FAILURE; }
