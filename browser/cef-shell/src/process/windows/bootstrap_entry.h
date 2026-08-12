#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_WINDOWS_BOOTSTRAP_ENTRY_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_WINDOWS_BOOTSTRAP_ENTRY_H_

#include <windows.h>

namespace crayon::browser::cef_shell::process {

int RunBrowserProcess(HINSTANCE bootstrap_instance, void *sandbox_info);

} // namespace crayon::browser::cef_shell::process

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_PROCESS_WINDOWS_BOOTSTRAP_ENTRY_H_
