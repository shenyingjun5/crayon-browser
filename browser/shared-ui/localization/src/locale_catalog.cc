#include "crayon/browser_localization/locale_catalog.h"

#include "generated/locale_catalog_data.h"

namespace crayon::browser::localization {

std::optional<std::string_view> LocaleCatalog::Find(
    std::string_view key) const noexcept {
  for (const generated::LocaleCatalogEntry& entry :
       generated::kLocaleCatalogEntries) {
    if (entry.key != key) {
      continue;
    }
    switch (locale_) {
      case AppLocale::kZhCn:
        return entry.zh_cn;
      case AppLocale::kZhTw:
        return entry.zh_tw;
      case AppLocale::kEnUs:
        return entry.en_us;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::size_t LocaleCatalog::Size() noexcept {
  return generated::kLocaleCatalogEntries.size();
}

std::string_view LocaleCatalog::Version() noexcept {
  return generated::kCatalogVersion;
}

}  // namespace crayon::browser::localization
