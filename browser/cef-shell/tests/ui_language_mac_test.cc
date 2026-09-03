#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_localization/locale_snapshot.h"
#include "process/macos/ui_language_mac.h"

namespace {

using crayon::browser::cef_shell::process::MacPreferredUiLanguages;
using crayon::browser::cef_shell::process::ResolveMacLocaleSnapshot;
using crayon::browser::localization::AppLocale;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << "check failed: " #condition << '\n';    \
      return false;                                         \
    }                                                       \
  } while (false)

bool ResolvesSupportedTagsAndOrdering() {
  CHECK(ResolveMacLocaleSnapshot({true, {"zh-Hant-HK", "en-US"}}).locale ==
        AppLocale::kZhTw);
  CHECK(ResolveMacLocaleSnapshot({true, {"zh-Hans", "zh-TW"}}).locale ==
        AppLocale::kZhCn);
  CHECK(ResolveMacLocaleSnapshot({true, {"en-GB", "zh-TW"}}).locale ==
        AppLocale::kEnUs);
  CHECK(ResolveMacLocaleSnapshot({true, {"ja-JP", "zh-TW"}}).locale ==
        AppLocale::kZhTw);
  return true;
}

bool InvalidApiDataFailsClosedToEnglish() {
  CHECK(ResolveMacLocaleSnapshot({false, {"zh-TW"}}).locale ==
        AppLocale::kEnUs);
  CHECK(ResolveMacLocaleSnapshot({true, {}}).locale == AppLocale::kEnUs);
  CHECK(ResolveMacLocaleSnapshot({true, {std::string(65, 'x')}}).locale ==
        AppLocale::kEnUs);
  CHECK(ResolveMacLocaleSnapshot(
            {true,
             std::vector<std::string>(
                 crayon::browser::localization::
                         kMaximumPreferredLocaleCount +
                     1U,
                 "zh-CN")})
            .locale == AppLocale::kEnUs);
  return true;
}

}  // namespace

int main() {
  if (!ResolvesSupportedTagsAndOrdering() ||
      !InvalidApiDataFailsClosedToEnglish()) {
    return 1;
  }
  std::cout << "macOS UI language adapter contract passed\n";
  return 0;
}
