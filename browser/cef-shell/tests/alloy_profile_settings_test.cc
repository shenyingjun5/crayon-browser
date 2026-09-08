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

bool SynchronousCleanupCompletionContract() {
  AlloyProfileSettings *self = nullptr;
  AlloyProfileSettingsResult completion = AlloyProfileSettingsResult::kInactive;
  AlloyProfileSettings settings(
      {{}, {}, {}, [&](const ProfileId &, std::uint64_t generation) {
         completion = self->CompleteCleanup(generation, true);
         return true;
       }});
  self = &settings;
  CHECK(settings.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  const auto cleanup = settings.BeginCleanup();
  CHECK(cleanup && completion == AlloyProfileSettingsResult::kSuccess);
  CHECK(settings.CompleteCleanup(*cleanup, true) ==
        AlloyProfileSettingsResult::kStaleGeneration);
  CHECK(settings.BeginCleanup().has_value());

  AlloyProfileSettings *failed_self = nullptr;
  AlloyProfileSettingsResult failed_completion = AlloyProfileSettingsResult::kSuccess;
  AlloyProfileSettings failed(
      {{}, {}, {}, [&](const ProfileId &, std::uint64_t generation) {
         failed_completion =
             failed_self->CompleteCleanup(generation, false, "disk-busy");
         return true;
       }});
  failed_self = &failed;
  CHECK(failed.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(failed.BeginCleanup().has_value() &&
        failed_completion == AlloyProfileSettingsResult::kExternalFailure &&
        failed.picker().cleanup_failure_pending() &&
        failed.last_failure_token() == "disk-busy" && !failed.BeginCleanup());
  return true;
}

bool ShutdownAndNestedCallbacksAreSafe() {
  AlloyProfileSettings *self = nullptr;
  ProfileId copied = Profile("invalid");
  AlloyProfileSettings settings(
      {[&](const ProfileId &profile, ProfileEntryKind) {
         self->Shutdown();
         copied = profile;
         return true;
       }, {}, {}, {}});
  self = &settings;
  CHECK(settings.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(settings.AddProfile(Profile("profile-b"), "B", ProfileEntryKind::kRegular));
  CHECK(settings.SwitchTo("profile-b") == AlloyProfileSettingsResult::kInactive &&
        copied.value() == "profile-b" && settings.PreferencesFor("profile-a") == nullptr);

  unsigned nested_callbacks = 0;
  AlloyProfileSettings *nested_self = nullptr;
  AlloyProfileSettings nested(
      {[&](const ProfileId &, ProfileEntryKind) {
         ++nested_callbacks;
         return nested_self->SwitchTo("profile-b") == AlloyProfileSettingsResult::kBusy;
       }, {}, {}, {}});
  nested_self = &nested;
  CHECK(nested.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(nested.AddProfile(Profile("profile-b"), "B", ProfileEntryKind::kRegular));
  CHECK(nested.SwitchTo("profile-b") == AlloyProfileSettingsResult::kSuccess &&
        nested_callbacks == 1);

  std::string target = "profile-b";
  AlloyProfileSettings copied_target(
      {[&](const ProfileId &, ProfileEntryKind) {
         target = "profile-a";
         return true;
       }, {}, {}, {}});
  CHECK(copied_target.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(copied_target.AddProfile(Profile("profile-b"), "B", ProfileEntryKind::kRegular));
  CHECK(copied_target.SwitchTo(target) == AlloyProfileSettingsResult::kSuccess &&
        copied_target.picker().active_profile() == "profile-b");

  AlloyProfileSettings *incognito_self = nullptr;
  ProfileId incognito_profile = Profile("invalid");
  AlloyProfileSettings incognito(
      {{}, [&](const ProfileId &profile, std::uint64_t) {
         incognito_self->Shutdown();
         incognito_profile = profile;
         return true;
       }, {}, {}});
  incognito_self = &incognito;
  CHECK(incognito.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(incognito.OpenIncognito() == AlloyProfileSettingsResult::kInactive &&
        incognito_profile.value() == "profile-a" &&
        incognito.PreferencesFor("profile-a") == nullptr);

  AlloyProfileSettings *apply_self = nullptr;
  std::int64_t saved_theme = PreferenceStore::kThemeLight;
  AlloyProfileSettings apply(
      {{}, {}, [&](const ProfileId &profile, const PreferenceStore &candidate) {
         apply_self->Shutdown();
         saved_theme = std::get<std::int64_t>(candidate.Get(PreferenceStore::kTheme));
         return profile.value() == "profile-a";
       }, {}});
  apply_self = &apply;
  CHECK(apply.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(apply.ApplyPreference(PreferenceStore::kTheme,
                              PreferenceValue{PreferenceStore::kThemeDark}) ==
            AlloyProfileSettingsResult::kInactive &&
        saved_theme == PreferenceStore::kThemeDark &&
        apply.PreferencesFor("profile-a") == nullptr);

  AlloyProfileSettings *reset_self = nullptr;
  std::int64_t reset_theme = PreferenceStore::kThemeDark;
  AlloyProfileSettings reset(
      {{}, {}, [&](const ProfileId &profile, const PreferenceStore &candidate) {
         reset_self->Shutdown();
         reset_theme = std::get<std::int64_t>(candidate.Get(PreferenceStore::kTheme));
         return profile.value() == "profile-a";
       }, {}});
  reset_self = &reset;
  PreferenceStore reset_preferences;
  CHECK(reset_preferences.Set(PreferenceStore::kTheme,
                              PreferenceValue{PreferenceStore::kThemeDark}));
  CHECK(reset.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular,
                         reset_preferences));
  CHECK(reset.RequestReset());
  CHECK(reset.ConfirmReset() == AlloyProfileSettingsResult::kInactive &&
        reset_theme == PreferenceStore::kThemeSystem &&
        reset.PreferencesFor("profile-a") == nullptr);

  AlloyProfileSettings *cleanup_self = nullptr;
  ProfileId cleanup_profile = Profile("invalid");
  AlloyProfileSettings cleanup(
      {{}, {}, {}, [&](const ProfileId &profile, std::uint64_t) {
         cleanup_self->Shutdown();
         cleanup_profile = profile;
         return true;
       }});
  cleanup_self = &cleanup;
  CHECK(cleanup.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(!cleanup.BeginCleanup() && cleanup_profile.value() == "profile-a" &&
        cleanup.PreferencesFor("profile-a") == nullptr);

  unsigned save_callbacks = 0;
  AlloyProfileSettings *save_self = nullptr;
  AlloyProfileSettings save_nested(
      {{}, {}, [&](const ProfileId &, const PreferenceStore &) {
         ++save_callbacks;
         return save_self->ApplyPreference(PreferenceStore::kTheme,
                                           PreferenceValue{PreferenceStore::kThemeDark}) ==
                    AlloyProfileSettingsResult::kBusy &&
                save_self->SwitchTo("profile-b") == AlloyProfileSettingsResult::kBusy &&
                !save_self->BeginCleanup();
       }, {}});
  save_self = &save_nested;
  CHECK(save_nested.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(save_nested.AddProfile(Profile("profile-b"), "B", ProfileEntryKind::kRegular));
  CHECK(save_nested.ApplyPreference(PreferenceStore::kTheme,
                                    PreferenceValue{PreferenceStore::kThemeDark}) ==
            AlloyProfileSettingsResult::kSuccess &&
        save_callbacks == 1);
  return true;
}

bool CleanupStartRejectionCanRetry() {
  unsigned starts = 0;
  AlloyProfileSettings settings(
      {{}, {}, {}, [&](const ProfileId &, std::uint64_t) { return ++starts != 1; }});
  CHECK(settings.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(!settings.BeginCleanup() && settings.picker().cleanup_failure_pending());
  settings.AcknowledgeCleanupFailure();
  const auto retry = settings.BeginCleanup();
  CHECK(retry && settings.CompleteCleanup(*retry, true) ==
                     AlloyProfileSettingsResult::kSuccess);

  AlloyProfileSettings *completed_self = nullptr;
  AlloyProfileSettings completed(
      {{}, {}, {}, [&](const ProfileId &, std::uint64_t generation) {
         CHECK(completed_self->CompleteCleanup(generation, false, "disk-busy") ==
               AlloyProfileSettingsResult::kExternalFailure);
         return false;
       }});
  completed_self = &completed;
  CHECK(completed.AddProfile(Profile("profile-a"), "A", ProfileEntryKind::kRegular));
  CHECK(!completed.BeginCleanup() &&
        completed.last_failure_token() == "disk-busy" &&
        completed.picker().cleanup_failure_pending());
  return true;
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

int main() {
  return SynchronousCleanupCompletionContract() &&
                 ShutdownAndNestedCallbacksAreSafe() && CleanupStartRejectionCanRetry() &&
                 ProfileSettingsContract()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
