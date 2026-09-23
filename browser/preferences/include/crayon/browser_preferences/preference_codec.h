#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "crayon/browser_preferences/preference_store.h"

namespace crayon::browser_preferences {

/// Maximum accepted preference file size (256 KiB).
inline constexpr std::size_t kMaxPreferenceFileBytes = 256 * 1024;

/// Current on-disk schema version.
///
/// v2 (PLT-SHELL-24M2FIX-C11) removed the `new_tab_url` key. A version 1
/// document that still carries it now migrates tolerantly instead of
/// failing as a whole, so an existing profile keeps every other
/// preference.
inline constexpr std::uint32_t kPreferenceSchemaVersion = 2;

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

/// Parses a document.  Current-schema documents are strict; older
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
