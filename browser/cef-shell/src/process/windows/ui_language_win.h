#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_WINDOWS_UI_LANGUAGE_WIN_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_WINDOWS_UI_LANGUAGE_WIN_H_

#include <string>
#include <vector>

#include "crayon/browser_localization/locale_snapshot.h"

namespace crayon::browser::cef_shell::process {

struct WindowsPreferredUiLanguages {
  bool api_succeeded = false;
  std::vector<std::wstring> language_tags;
};

WindowsPreferredUiLanguages ReadWindowsPreferredUiLanguages();

::crayon::browser::localization::LocaleSnapshot ResolveWindowsLocaleSnapshot(
    const WindowsPreferredUiLanguages& preferred_languages);

}  // namespace crayon::browser::cef_shell::process

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_WINDOWS_UI_LANGUAGE_WIN_H_
