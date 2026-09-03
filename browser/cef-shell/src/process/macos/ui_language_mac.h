#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_MACOS_UI_LANGUAGE_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_MACOS_UI_LANGUAGE_MAC_H_

#include <string>
#include <vector>

#include "crayon/browser_localization/locale_snapshot.h"

namespace crayon::browser::cef_shell::process {

struct MacPreferredUiLanguages {
  bool api_succeeded = false;
  std::vector<std::string> language_tags;
};

MacPreferredUiLanguages ReadMacPreferredUiLanguages();

::crayon::browser::localization::LocaleSnapshot ResolveMacLocaleSnapshot(
    const MacPreferredUiLanguages& preferred_languages);

}  // namespace crayon::browser::cef_shell::process

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_MACOS_UI_LANGUAGE_MAC_H_
