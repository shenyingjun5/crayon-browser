#include "process/macos/ui_language_mac.h"

#include <cstddef>

namespace crayon::browser::cef_shell::process {

::crayon::browser::localization::LocaleSnapshot ResolveMacLocaleSnapshot(
    const MacPreferredUiLanguages& preferred_languages) {
  using ::crayon::browser::localization::AppLocale;
  using ::crayon::browser::localization::ResolveLocaleSnapshot;
  using ::crayon::browser::localization::SnapshotFor;

  if (!preferred_languages.api_succeeded ||
      preferred_languages.language_tags.empty() ||
      preferred_languages.language_tags.size() >
          ::crayon::browser::localization::kMaximumPreferredLocaleCount) {
    return SnapshotFor(AppLocale::kEnUs);
  }

  std::size_t total_bytes = 0;
  for (const std::string& tag : preferred_languages.language_tags) {
    if (tag.empty() ||
        tag.size() >
            ::crayon::browser::localization::kMaximumLocaleTagBytes) {
      return SnapshotFor(AppLocale::kEnUs);
    }
    total_bytes += tag.size();
    if (total_bytes >
        ::crayon::browser::localization::kMaximumPreferredLocaleBytes) {
      return SnapshotFor(AppLocale::kEnUs);
    }
  }
  return ResolveLocaleSnapshot(preferred_languages.language_tags);
}

}  // namespace crayon::browser::cef_shell::process
