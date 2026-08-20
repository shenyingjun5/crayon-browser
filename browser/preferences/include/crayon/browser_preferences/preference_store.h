#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace crayon::browser_preferences {

/// Maximum length of a string preference value in bytes.
inline constexpr std::size_t kMaxPreferenceStringBytes = 1024;

/// Typed preference value.  The closed variant set keeps the store free of
/// arbitrary payloads.
using PreferenceValue = std::variant<bool, std::int64_t, std::string>;

/// Store command failure.  Stable variants carry no user data.
enum class PreferenceError {
  kUnknownKey = 0,
  kTypeMismatch,
  kInvalidValue,
};

/// Versioned typed preference store with a closed key registry.
///
/// Keys are fixed at compile time; each key has a fixed type and default.
/// Unknown keys and type mismatches are stable rejections.  Thread
/// contract: single-threaded, UI thread only.
class PreferenceStore final {
 public:
  // --- Closed key registry ---
  static constexpr char kStartupPolicy[] = "startup_policy";      // int enum
  static constexpr char kTheme[] = "theme";                        // int enum
  static constexpr char kShowBookmarkBar[] = "show_bookmark_bar";  // bool
  static constexpr char kDownloadDirectory[] = "download_directory";  // string
  static constexpr char kSearchProvider[] = "search_provider";     // string

  // startup_policy values.
  static constexpr std::int64_t kStartupNewTab = 0;
  static constexpr std::int64_t kStartupRestore = 1;

  // theme values.
  static constexpr std::int64_t kThemeSystem = 0;
  static constexpr std::int64_t kThemeLight = 1;
  static constexpr std::int64_t kThemeDark = 2;

  PreferenceStore();  // All keys at defaults.

  /// Sets a key after type and value validation.
  bool Set(const std::string& key,
           PreferenceValue value,
           PreferenceError* error = nullptr);

  /// Reads a key; returns its default when never explicitly set.
  const PreferenceValue& Get(const std::string& key) const;

  /// Reports whether a key currently differs from its default.
  bool IsModified(const std::string& key) const;

  /// Restores one key (or all keys) to defaults.
  bool Reset(const std::string& key);
  void ResetAll() noexcept;

  /// Lists every registered key in stable order.
  static const std::vector<std::string>& RegisteredKeys();

  /// Validates a value for a key without mutating the store.
  static bool IsValidValueForKey(const std::string& key,
                                 const PreferenceValue& value) noexcept;

 private:
  struct KeySpec final {
    PreferenceValue default_value;
  };

  static const std::unordered_map<std::string, KeySpec>& Registry();

  std::unordered_map<std::string, PreferenceValue> overrides_;
};

}  // namespace crayon::browser_preferences
