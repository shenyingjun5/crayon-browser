#include <windows.h>

#include "include/cef_sandbox_win.h"
#include "include/cef_version_info.h"
#include "process/windows/bootstrap_entry.h"

namespace {

constexpr int kVersionInfoMissingExitCode = 9;

} // namespace

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPTSTR command_line,
                                    int show_command, void *sandbox_info,
                                    cef_version_info_t *version_info) {
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);
  if (!version_info) {
    return kVersionInfoMissingExitCode;
  }
  return crayon::browser::cef_shell::process::RunBrowserProcess(instance,
                                                                sandbox_info);
}
