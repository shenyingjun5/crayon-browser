#ifndef CRAYON_BROWSER_SHARED_UI_LOCALIZATION_INCLUDE_CRAYON_BROWSER_LOCALIZATION_LOCALE_CATALOG_H_
#define CRAYON_BROWSER_SHARED_UI_LOCALIZATION_INCLUDE_CRAYON_BROWSER_LOCALIZATION_LOCALE_CATALOG_H_

#include <cstddef>
#include <optional>
#include <string_view>

#include "crayon/browser_localization/locale_snapshot.h"

namespace crayon::browser::localization {

class LocaleCatalog final {
 public:
  explicit constexpr LocaleCatalog(AppLocale locale) noexcept
      : locale_(locale) {}

  AppLocale locale() const noexcept { return locale_; }
  std::optional<std::string_view> Find(std::string_view key) const noexcept;
  static std::size_t Size() noexcept;
  static std::string_view Version() noexcept;

 private:
  AppLocale locale_;
};

}  // namespace crayon::browser::localization

#endif  // CRAYON_BROWSER_SHARED_UI_LOCALIZATION_INCLUDE_CRAYON_BROWSER_LOCALIZATION_LOCALE_CATALOG_H_
