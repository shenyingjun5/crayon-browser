#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "crayon/browser_preferences/preference_codec.h"
#include "crayon/browser_preferences/preference_store.h"

namespace {

using crayon::browser_preferences::DeserializePreferences;
using crayon::browser_preferences::LoadPreferencesFromFile;
using crayon::browser_preferences::PreferenceCodecError;
using crayon::browser_preferences::PreferenceError;
using crayon::browser_preferences::PreferenceStore;
using crayon::browser_preferences::PreferenceValue;
using crayon::browser_preferences::SavePreferencesToFile;
using crayon::browser_preferences::SerializePreferences;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// ---------- Store ----------

bool DefaultsAndSetGet() {
  PreferenceStore store;
  CHECK(std::get<std::int64_t>(
            store.Get(PreferenceStore::kStartupPolicy)) ==
        PreferenceStore::kStartupNewTab);
  CHECK(!std::get<bool>(store.Get(PreferenceStore::kShowBookmarkBar)));
  CHECK(!store.IsModified(PreferenceStore::kTheme));

  CHECK(store.Set(PreferenceStore::kTheme, PreferenceValue{PreferenceStore::kThemeDark}));
  CHECK(store.IsModified(PreferenceStore::kTheme));
  CHECK(std::get<std::int64_t>(store.Get(PreferenceStore::kTheme)) ==
        PreferenceStore::kThemeDark);
  // Setting back to the default clears the override.
  CHECK(store.Set(PreferenceStore::kTheme, PreferenceValue{PreferenceStore::kThemeSystem}));
  CHECK(!store.IsModified(PreferenceStore::kTheme));
  return true;
}

bool TypeAndKeyRejection() {
  PreferenceStore store;
  PreferenceError error = PreferenceError::kUnknownKey;
  CHECK(!store.Set("no_such_key", PreferenceValue{true}, &error));
  CHECK(error == PreferenceError::kUnknownKey);
  CHECK(!store.Set(PreferenceStore::kTheme, PreferenceValue{true}, &error));
  CHECK(error == PreferenceError::kTypeMismatch);
  CHECK(!store.IsModified(PreferenceStore::kTheme));
  return true;
}

bool ValueValidation() {
  PreferenceStore store;
  PreferenceError error = PreferenceError::kUnknownKey;
  // Out-of-range enum.
  CHECK(!store.Set(PreferenceStore::kTheme, PreferenceValue{std::int64_t{9}},
                   &error));
  CHECK(error == PreferenceError::kInvalidValue);
  // Oversize / control-char string.
  CHECK(!store.Set(PreferenceStore::kSearchProvider,
                   PreferenceValue{std::string(1025, 'x')}, &error));
  CHECK(error == PreferenceError::kInvalidValue);
  CHECK(!store.Set(PreferenceStore::kDownloadDirectory,
                   PreferenceValue{std::string("/dl\x07")}, &error));
  CHECK(error == PreferenceError::kInvalidValue);
  CHECK(store.Set(PreferenceStore::kDownloadDirectory,
                  PreferenceValue{std::string("/downloads")}, &error));
  return true;
}

bool ResetSemantics() {
  PreferenceStore store;
  store.Set(PreferenceStore::kTheme, PreferenceValue{PreferenceStore::kThemeDark});
  store.Set(PreferenceStore::kShowBookmarkBar, PreferenceValue{true});
  CHECK(store.Reset(PreferenceStore::kTheme));
  CHECK(!store.IsModified(PreferenceStore::kTheme));
  CHECK(store.IsModified(PreferenceStore::kShowBookmarkBar));
  CHECK(!store.Reset("no_such_key"));
  store.ResetAll();
  CHECK(!store.IsModified(PreferenceStore::kShowBookmarkBar));
  return true;
}

// ---------- Codec ----------

bool RoundTripOnlyOverrides() {
  PreferenceStore store;
  store.Set(PreferenceStore::kStartupPolicy,
            PreferenceValue{PreferenceStore::kStartupRestore});
  store.Set(PreferenceStore::kSearchProvider,
            PreferenceValue{std::string("自定义 搜索")});
  const std::string document = SerializePreferences(store);
  CHECK(document.find("startup_policy") != std::string::npos);
  CHECK(document.find("show_bookmark_bar") == std::string::npos);  // default
  const auto restored = DeserializePreferences(document);
  CHECK(restored.has_value());
  for (const std::string& key : PreferenceStore::RegisteredKeys()) {
    CHECK(restored->Get(key) == store.Get(key));
    CHECK(restored->IsModified(key) == store.IsModified(key));
  }
  return true;
}

bool MigrationFromV0DropsUnknownAndInvalid() {
  const std::string v0 =
      "CRAYON-PREFERENCES v0\n"
      "I 14\nstartup_policy\n1\n"
      "B 7\nold_key\n1\n"
      "B 17\nshow_bookmark_bar\n9\n";  // invalid bool -> dropped
  const auto migrated = DeserializePreferences(v0);
  CHECK(migrated.has_value());
  CHECK(std::get<std::int64_t>(
            migrated->Get(PreferenceStore::kStartupPolicy)) ==
        PreferenceStore::kStartupRestore);
  CHECK(!migrated->IsModified(PreferenceStore::kShowBookmarkBar));
  return true;
}

bool StrictV1RejectsUnknownKeys() {
  const std::string document =
      "CRAYON-PREFERENCES v1\n"
      "B 7\nold_key\n1\n";
  PreferenceCodecError error = PreferenceCodecError::kIoFailure;
  CHECK(!DeserializePreferences(document, &error).has_value());
  CHECK(error == PreferenceCodecError::kContentRejected);
  return true;
}

bool CorruptionMatrixFailsClosed() {
  PreferenceCodecError error = PreferenceCodecError::kIoFailure;
  CHECK(!DeserializePreferences("CRAYON-PREFERENCES\n", &error).has_value());
  CHECK(error == PreferenceCodecError::kBadHeader);
  CHECK(!DeserializePreferences("CRAYON-PREFERENCES v2\n", &error)
             .has_value());
  CHECK(error == PreferenceCodecError::kUnsupportedVersion);
  CHECK(!DeserializePreferences("CRAYON-PREFERENCES v1\nX 1\nk\n1\n", &error)
             .has_value());
  CHECK(error == PreferenceCodecError::kUnknownRecordType);
  CHECK(!DeserializePreferences("CRAYON-PREFERENCES v1\nB 99999\nk\n", &error)
             .has_value());
  CHECK(error == PreferenceCodecError::kLengthOverflow);
  return true;
}

bool RestartReadbackIsIdentical() {
  const std::string path =
      std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR")
                                                   : "/tmp") +
      "/crayon-preferences-test-v1.txt";
  PreferenceStore store;
  store.Set(PreferenceStore::kTheme, PreferenceValue{PreferenceStore::kThemeLight});
  store.Set(PreferenceStore::kShowBookmarkBar, PreferenceValue{true});
  store.Set(PreferenceStore::kSearchProvider,
            PreferenceValue{std::string("provider-a")});
  PreferenceCodecError error = PreferenceCodecError::kIoFailure;
  CHECK(SavePreferencesToFile(store, path, &error));
  const auto reloaded = LoadPreferencesFromFile(path, &error);
  CHECK(reloaded.has_value());
  for (const std::string& key : PreferenceStore::RegisteredKeys()) {
    CHECK(reloaded->Get(key) == store.Get(key));
  }
  std::ifstream staging(path + ".tmp");
  CHECK(!staging.good());
  std::remove(path.c_str());
  // Missing file fails closed.
  CHECK(!LoadPreferencesFromFile("/nonexistent/crayon-pref-none.txt", &error)
             .has_value());
  CHECK(error == PreferenceCodecError::kIoFailure);
  return true;
}

}  // namespace

int main() {
  if (!DefaultsAndSetGet() || !TypeAndKeyRejection() || !ValueValidation() ||
      !ResetSemantics() || !RoundTripOnlyOverrides() ||
      !MigrationFromV0DropsUnknownAndInvalid() ||
      !StrictV1RejectsUnknownKeys() || !CorruptionMatrixFailsClosed() ||
      !RestartReadbackIsIdentical()) {
    return 1;
  }
  return 0;
}
