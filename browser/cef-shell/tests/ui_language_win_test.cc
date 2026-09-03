#include <iostream>
#include <string>
#include <vector>

#include "process/windows/ui_language_win.h"

namespace {

using crayon::browser::cef_shell::process::ResolveWindowsLocaleSnapshot;
using crayon::browser::cef_shell::process::WindowsPreferredUiLanguages;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool SupportedLanguagesAndOrderAreExact() {
  auto snapshot = ResolveWindowsLocaleSnapshot(
      WindowsPreferredUiLanguages{true, {L"zh-Hant-HK", L"en-US"}});
  CHECK(snapshot.cef_locale == "zh-TW");
  CHECK(snapshot.html_language == "zh-TW");
  CHECK(snapshot.accept_language_list == "zh-TW,zh,en-US,en");

  snapshot = ResolveWindowsLocaleSnapshot(
      WindowsPreferredUiLanguages{true, {L"fr-FR", L"en-GB", L"zh-CN"}});
  CHECK(snapshot.cef_locale == "en-US");
  CHECK(snapshot.accept_language_list == "en-US,en");

  snapshot = ResolveWindowsLocaleSnapshot(
      WindowsPreferredUiLanguages{true, {L"zh_SG"}});
  CHECK(snapshot.cef_locale == "zh-CN");
  return true;
}

bool FailureAndHostileInputsFailClosed() {
  CHECK(ResolveWindowsLocaleSnapshot({false, {L"zh-CN"}}).cef_locale ==
        "en-US");
  CHECK(ResolveWindowsLocaleSnapshot({true, {}}).cef_locale == "en-US");
  CHECK(ResolveWindowsLocaleSnapshot({true, {std::wstring(65, L'a')}})
            .cef_locale == "en-US");

  WindowsPreferredUiLanguages excessive;
  excessive.api_succeeded = true;
  excessive.language_tags.assign(33, L"fr-FR");
  CHECK(ResolveWindowsLocaleSnapshot(excessive).cef_locale == "en-US");

  const std::wstring invalid_surrogate(1, static_cast<wchar_t>(0xD800));
  CHECK(ResolveWindowsLocaleSnapshot({true, {invalid_surrogate}}).cef_locale ==
        "en-US");
  return true;
}

bool RealApiProducesAClosedSnapshot() {
  const auto snapshot = ResolveWindowsLocaleSnapshot(
      crayon::browser::cef_shell::process::ReadWindowsPreferredUiLanguages());
  CHECK(snapshot.cef_locale == "en-US" || snapshot.cef_locale == "zh-CN" ||
        snapshot.cef_locale == "zh-TW");
  CHECK(!snapshot.accept_language_list.empty());
  return true;
}

}  // namespace

int main() {
  return SupportedLanguagesAndOrderAreExact() &&
                 FailureAndHostileInputsFailClosed() &&
                 RealApiProducesAClosedSnapshot()
             ? 0
             : 1;
}
