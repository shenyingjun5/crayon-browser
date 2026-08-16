#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_NEW_TAB_CEF_NEW_TAB_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_NEW_TAB_CEF_NEW_TAB_HANDLER_H_

#include "crayon/browser_new_tab/new_tab_page.h"
#include "include/cef_app.h"
#include "include/cef_scheme.h"

namespace crayon::browser::cef_shell::new_tab {

void RegisterCrayonCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar);
CefRefPtr<CefApp> CreateNewTabProcessApp();
bool RegisterNewTabSchemeHandlerFactory(
    browser_new_tab::NewTabPageModel page_model,
    browser_new_tab::NewTabPageStrings strings);

}  // namespace crayon::browser::cef_shell::new_tab

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_NEW_TAB_CEF_NEW_TAB_HANDLER_H_
