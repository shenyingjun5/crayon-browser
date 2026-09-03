#ifndef CRAYON_BROWSER_SHARED_UI_LOCALIZATION_INCLUDE_CRAYON_BROWSER_LOCALIZATION_LOCALE_SNAPSHOT_H_
#define CRAYON_BROWSER_SHARED_UI_LOCALIZATION_INCLUDE_CRAYON_BROWSER_LOCALIZATION_LOCALE_SNAPSHOT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crayon::browser::localization {

inline constexpr std::size_t kMaximumLocaleTagBytes = 64;
inline constexpr std::size_t kMaximumPreferredLocaleCount = 32;
inline constexpr std::size_t kMaximumPreferredLocaleBytes = 4096;

enum class AppLocale : std::uint8_t {
  kEnUs,
  kZhCn,
  kZhTw,
};

struct LocaleSnapshot {
  AppLocale locale = AppLocale::kEnUs;
  std::string_view tag = "en-US";
  std::string_view cef_locale = "en-US";
  std::string_view html_language = "en-US";
  std::string_view accept_language_list = "en-US,en";
};

LocaleSnapshot SnapshotFor(AppLocale locale) noexcept;

std::optional<AppLocale> ResolveLocaleTag(std::string_view tag) noexcept;

LocaleSnapshot ResolveLocaleSnapshot(
    const std::vector<std::string>& preferred_languages) noexcept;

}  // namespace crayon::browser::localization

#endif  // CRAYON_BROWSER_SHARED_UI_LOCALIZATION_INCLUDE_CRAYON_BROWSER_LOCALIZATION_LOCALE_SNAPSHOT_H_
