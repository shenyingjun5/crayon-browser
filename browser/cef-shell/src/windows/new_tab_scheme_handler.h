#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_NEW_TAB_SCHEME_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_NEW_TAB_SCHEME_HANDLER_H_

#include "crayon/new_tab/new_tab.h"
#include "include/cef_app.h"

namespace crayon::browser::cef_shell {

void RegisterCrayonScheme(CefRawPtr<CefSchemeRegistrar> registrar);

bool RegisterNewTabSchemeHandler(
    crayon::browser::new_tab::NewTabStrings strings);

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_NEW_TAB_SCHEME_HANDLER_H_
