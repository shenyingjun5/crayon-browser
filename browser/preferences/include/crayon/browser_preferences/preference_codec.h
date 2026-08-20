#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "crayon/browser_preferences/preference_store.h"

namespace crayon::browser_preferences {

/// Maximum accepted preference file size (256 KiB).
inline constexpr std::size_t kMaxPreferenceFileBytes = 256 * 1024;

/// Current on-disk schema version.
inline constexpr std::uint32_t kPreferenceSchemaVersion = 1;

/// Codec failure.  Stable variants carry no file content or paths.
enum class PreferenceCodecError {
  kBadHeader = 0,
  kUnsupportedVersion,
  kTruncated,
  kLengthOverflow,
  kUnknownRecordType,
  /// Same-version document contained an unknown key or invalid value.
  kContentRejected,
  kIoFailure,
};

/// Serializes only non-default overrides, in registered key order.
std::string SerializePreferences(const PreferenceStore& store);

/// Parses a document.  `schema=1` documents are strict; `schema=0`
/// documents are migrated tolerantly (unknown keys and invalid values are
/// dropped, everything else takes defaults).  Newer schemas and any
/// structural corruption fail closed.
std::optional<PreferenceStore> DeserializePreferences(
    const std::string& document,
    PreferenceCodecError* error = nullptr);

/// Atomically persists via `<path>.tmp` + rename.
bool SavePreferencesToFile(const PreferenceStore& store,
                           const std::string& path,
                           PreferenceCodecError* error = nullptr);

/// Loads from disk; missing/unreadable/oversized/corrupt files fail closed.
std::optional<PreferenceStore> LoadPreferencesFromFile(
    const std::string& path,
    PreferenceCodecError* error = nullptr);

}  // namespace crayon::browser_preferences
