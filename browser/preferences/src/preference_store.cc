#include "crayon/browser_preferences/preference_store.h"

#include <string_view>

namespace crayon::browser_preferences {

namespace {

bool HasControlChars(std::string_view text) noexcept {
  for (const char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7F) {
      return true;
    }
  }
  return false;
}

void SetError(PreferenceError* error, PreferenceError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

}  // namespace

const std::unordered_map<std::string, PreferenceStore::KeySpec>&
PreferenceStore::Registry() {
  static const std::unordered_map<std::string, KeySpec> registry = {
      {kStartupPolicy, KeySpec{PreferenceValue{kStartupNewTab}}},
      {kTheme, KeySpec{PreferenceValue{kThemeSystem}}},
      {kShowBookmarkBar, KeySpec{PreferenceValue{false}}},
      {kDownloadDirectory, KeySpec{PreferenceValue{std::string{}}}},
      {kSearchProvider, KeySpec{PreferenceValue{std::string{}}}},
  };
  return registry;
}

const std::vector<std::string>& PreferenceStore::RegisteredKeys() {
  static const std::vector<std::string> keys = {
      kStartupPolicy, kTheme, kShowBookmarkBar, kDownloadDirectory,
      kSearchProvider,
  };
  return keys;
}

PreferenceStore::PreferenceStore() = default;

bool PreferenceStore::IsValidValueForKey(
    const std::string& key,
    const PreferenceValue& value) noexcept {
  const auto spec = Registry().find(key);
  if (spec == Registry().end()) {
    return false;
  }
  if (value.index() != spec->second.default_value.index()) {
    return false;
  }
  if (const auto* number = std::get_if<std::int64_t>(&value)) {
    if (key == kStartupPolicy) {
      return *number == kStartupNewTab || *number == kStartupRestore;
    }
    if (key == kTheme) {
      return *number == kThemeSystem || *number == kThemeLight ||
             *number == kThemeDark;
    }
    return true;
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return text->size() <= kMaxPreferenceStringBytes &&
           !HasControlChars(*text);
  }
  return true;  // bool
}

bool PreferenceStore::Set(const std::string& key,
                          PreferenceValue value,
                          PreferenceError* error) {
  const auto spec = Registry().find(key);
  if (spec == Registry().end()) {
    SetError(error, PreferenceError::kUnknownKey);
    return false;
  }
  if (value.index() != spec->second.default_value.index()) {
    SetError(error, PreferenceError::kTypeMismatch);
    return false;
  }
  if (!IsValidValueForKey(key, value)) {
    SetError(error, PreferenceError::kInvalidValue);
    return false;
  }
  if (value == spec->second.default_value) {
    overrides_.erase(key);  // Setting the default clears the override.
  } else {
    overrides_[key] = std::move(value);
  }
  return true;
}

const PreferenceValue& PreferenceStore::Get(const std::string& key) const {
  const auto overriden = overrides_.find(key);
  if (overriden != overrides_.end()) {
    return overriden->second;
  }
  const auto spec = Registry().find(key);
  // Unknown keys yield the registered default of the first key only as a
  // defensive placeholder; callers must check keys via RegisteredKeys().
  static const PreferenceValue kFallback{false};
  return spec == Registry().end() ? kFallback : spec->second.default_value;
}

bool PreferenceStore::IsModified(const std::string& key) const {
  return overrides_.count(key) != 0;
}

bool PreferenceStore::Reset(const std::string& key) {
  if (Registry().count(key) == 0) {
    return false;
  }
  overrides_.erase(key);
  return true;
}

void PreferenceStore::ResetAll() noexcept {
  overrides_.clear();
}

}  // namespace crayon::browser_preferences
