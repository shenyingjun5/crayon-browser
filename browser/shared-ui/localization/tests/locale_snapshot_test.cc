#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_localization/locale_catalog.h"
#include "crayon/browser_localization/locale_snapshot.h"

namespace {

using crayon::browser::localization::AppLocale;
using crayon::browser::localization::LocaleSnapshot;
using crayon::browser::localization::LocaleCatalog;
using crayon::browser::localization::ResolveLocaleSnapshot;
using crayon::browser::localization::ResolveLocaleTag;
using crayon::browser::localization::SnapshotFor;
using crayon::browser::localization::kMaximumLocaleTagBytes;
using crayon::browser::localization::kMaximumPreferredLocaleBytes;
using crayon::browser::localization::kMaximumPreferredLocaleCount;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool IsLocale(std::string tag, AppLocale expected) {
  const auto locale = ResolveLocaleTag(tag);
  CHECK(locale.has_value());
  CHECK(*locale == expected);
  return true;
}

bool CanonicalMappingsAreStable() {
  CHECK(IsLocale("zh-CN", AppLocale::kZhCn));
  CHECK(IsLocale("ZH_cn", AppLocale::kZhCn));
  CHECK(IsLocale("zh-Hans", AppLocale::kZhCn));
  CHECK(IsLocale("zh-Hans-SG", AppLocale::kZhCn));
  CHECK(IsLocale("zh-SG", AppLocale::kZhCn));
  CHECK(IsLocale("zh", AppLocale::kZhCn));
  CHECK(IsLocale("zh-TW", AppLocale::kZhTw));
  CHECK(IsLocale("zh_hant_TW", AppLocale::kZhTw));
  CHECK(IsLocale("zh-HK", AppLocale::kZhTw));
  CHECK(IsLocale("zh-MO", AppLocale::kZhTw));
  CHECK(IsLocale("en", AppLocale::kEnUs));
  CHECK(IsLocale("en-US", AppLocale::kEnUs));
  CHECK(IsLocale("EN_gb", AppLocale::kEnUs));
  return true;
}

bool InvalidAndUnsupportedTagsAreIgnored() {
  for (const char* tag :
       {"", "-en", "en-", "en--US", "en US", "zh-Latn", "fr-FR",
        "zh-繁體", "_", "x-private"}) {
    CHECK(!ResolveLocaleTag(tag).has_value());
  }
  CHECK(!ResolveLocaleTag(std::string(kMaximumLocaleTagBytes + 1, 'a'))
             .has_value());
  return true;
}

bool OrderedListAndFallbackAreStable() {
  CHECK(ResolveLocaleSnapshot({"fr-FR", "zh-Hant", "en-US"}).locale ==
        AppLocale::kZhTw);
  CHECK(ResolveLocaleSnapshot({"de-DE", "EN-au", "zh-CN"}).locale ==
        AppLocale::kEnUs);
  CHECK(ResolveLocaleSnapshot({"bad tag", "zh-SG"}).locale ==
        AppLocale::kZhCn);
  CHECK(ResolveLocaleSnapshot({}).locale == AppLocale::kEnUs);
  CHECK(ResolveLocaleSnapshot({"fr-FR", "de-DE"}).locale ==
        AppLocale::kEnUs);
  return true;
}

bool InputBudgetsFailClosed() {
  std::vector<std::string> too_many(kMaximumPreferredLocaleCount + 1,
                                    "zh-CN");
  CHECK(ResolveLocaleSnapshot(too_many).locale == AppLocale::kEnUs);

  std::vector<std::string> too_large;
  too_large.reserve(kMaximumPreferredLocaleCount);
  const std::size_t bytes_per_tag =
      kMaximumPreferredLocaleBytes / kMaximumPreferredLocaleCount + 1;
  for (std::size_t index = 0; index < kMaximumPreferredLocaleCount; ++index) {
    too_large.emplace_back(bytes_per_tag, 'a');
  }
  CHECK(ResolveLocaleSnapshot(too_large).locale == AppLocale::kEnUs);
  return true;
}

bool ProjectionIsClosedAndExact() {
  const LocaleSnapshot english = SnapshotFor(AppLocale::kEnUs);
  CHECK(english.tag == "en-US");
  CHECK(english.cef_locale == "en-US");
  CHECK(english.html_language == "en-US");
  CHECK(english.accept_language_list == "en-US,en");

  const LocaleSnapshot simplified = SnapshotFor(AppLocale::kZhCn);
  CHECK(simplified.tag == "zh-CN");
  CHECK(simplified.cef_locale == "zh-CN");
  CHECK(simplified.html_language == "zh-CN");
  CHECK(simplified.accept_language_list == "zh-CN,zh,en-US,en");

  const LocaleSnapshot traditional = SnapshotFor(AppLocale::kZhTw);
  CHECK(traditional.tag == "zh-TW");
  CHECK(traditional.cef_locale == "zh-TW");
  CHECK(traditional.html_language == "zh-TW");
  CHECK(traditional.accept_language_list == "zh-TW,zh,en-US,en");
  return true;
}

bool CatalogIsClosedAndDoesNotFallback() {
  CHECK(LocaleCatalog::Size() == 156);
  CHECK(LocaleCatalog::Version() == "desktop-localization-v1");
  const LocaleCatalog english(AppLocale::kEnUs);
  const LocaleCatalog simplified(AppLocale::kZhCn);
  const LocaleCatalog traditional(AppLocale::kZhTw);
  CHECK(english.Find("app.title") == "Crayon AI Agent Cast Browser");
  CHECK(simplified.Find("app.title") == "蜡笔 AI Agent 投屏浏览器");
  CHECK(traditional.Find("app.title") == "蠟筆 AI Agent 投影瀏覽器");
  CHECK(english.Find("app.about") == "About Crayon Browser");
  CHECK(simplified.Find("app.about") == "关于蜡笔浏览器");
  CHECK(traditional.Find("app.about") == "關於蠟筆瀏覽器");
  CHECK(traditional.Find("mdv.view_source") == "原始碼");
  CHECK(!english.Find("missing.key").has_value());
  CHECK(!simplified.Find("").has_value());
  return true;
}

std::uint32_t NextRandom(std::uint32_t value) {
  return value * 1664525U + 1013904223U;
}

bool DeterministicFuzzProjectionStaysClosed() {
  constexpr char alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_@ ";
  std::uint32_t state = 0x43524159U;
  for (std::size_t step = 0; step < 5000; ++step) {
    state = NextRandom(state);
    const std::size_t length = state % 80U;
    std::string tag;
    tag.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
      state = NextRandom(state);
      tag.push_back(alphabet[state % (sizeof(alphabet) - 1)]);
    }
    const auto first = ResolveLocaleTag(tag);
    const auto second = ResolveLocaleTag(tag);
    CHECK(first == second);
    if (first) {
      const LocaleSnapshot snapshot = SnapshotFor(*first);
      CHECK(snapshot.locale == AppLocale::kEnUs ||
            snapshot.locale == AppLocale::kZhCn ||
            snapshot.locale == AppLocale::kZhTw);
      CHECK(!snapshot.cef_locale.empty());
      CHECK(!snapshot.accept_language_list.empty());
    }
  }
  return true;
}

}  // namespace

int main() {
  if (!CanonicalMappingsAreStable() || !InvalidAndUnsupportedTagsAreIgnored() ||
      !OrderedListAndFallbackAreStable() || !InputBudgetsFailClosed() ||
      !ProjectionIsClosedAndExact() || !CatalogIsClosedAndDoesNotFallback() ||
      !DeterministicFuzzProjectionStaysClosed()) {
    return 1;
  }
  return 0;
}
