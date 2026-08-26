#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_HANDLER_H_

#include "crayon/browser_mdv/mdv_page.h"
#include "include/cef_scheme.h"

namespace crayon::browser::cef_shell::mdv {

using crayon::browser_mdv::MdvPageStrings;

// Registers the crayon://mdv scheme handler factory (domain "mdv") with
// bodies rendered once from the compile-time fixture document through the
// shared MDV-03 viewer model and MDV-02 engine.  `strings` come from the
// platform string resources.  Must be called on the CEF UI thread during
// OnContextInitialized, after the new-tab factory.
bool RegisterMdvSchemeHandlerFactory(MdvPageStrings strings);

}  // namespace crayon::browser::cef_shell::mdv

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_HANDLER_H_
