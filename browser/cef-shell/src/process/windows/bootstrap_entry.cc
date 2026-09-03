#include "process/windows/bootstrap_entry.h"

#include <string>

#include "browser/new_tab/cef_new_tab_handler.h"
#include "include/cef_app.h"
#include "process/windows/ui_language_win.h"
#include "windows/app.h"

namespace crayon::browser::cef_shell::process {
namespace {

enum class ExitCode : int {
  kSuccess = 0,
  kBootstrapInstanceMissing = 10,
  kSandboxInfoMissing = 11,
  kClientModuleMissing = 12,
  kBrandIconMissing = 14,
  kNewTabStringsMissing = 15,
  kMdvStringsMissing = 16,
  kPageMarkdownStringsMissing = 17,
  kCastStringsMissing = 18,
  kCefInitializeFailed = 20,
};

HINSTANCE GetClientModule() {
  HMODULE client_module = nullptr;
  const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
  const auto address = reinterpret_cast<LPCWSTR>(&GetClientModule);
  if (!GetModuleHandleExW(flags, address, &client_module)) {
    return nullptr;
  }
  return client_module;
}

}  // namespace

int RunBrowserProcess(HINSTANCE bootstrap_instance, void *sandbox_info) {
  if (!bootstrap_instance) {
    return static_cast<int>(ExitCode::kBootstrapInstanceMissing);
  }
  if (!sandbox_info) {
    return static_cast<int>(ExitCode::kSandboxInfoMissing);
  }

  CefMainArgs main_args(bootstrap_instance);
  CefRefPtr<CefApp> child_app = new_tab::CreateNewTabProcessApp();
  const int child_exit_code =
      CefExecuteProcess(main_args, child_app, sandbox_info);
  if (child_exit_code >= 0) {
    return child_exit_code;
  }

  HINSTANCE client_module = GetClientModule();
  if (!client_module) {
    return static_cast<int>(ExitCode::kClientModuleMissing);
  }
  const auto locale_snapshot = ResolveWindowsLocaleSnapshot(
      ReadWindowsPreferredUiLanguages());
  CefSettings settings;
  settings.log_severity = LOGSEVERITY_DISABLE;
  CefString(&settings.locale) = std::string(locale_snapshot.cef_locale);
  CefString(&settings.accept_language_list) =
      std::string(locale_snapshot.accept_language_list);
  CefRefPtr<BrowserApp> app(new BrowserApp(client_module, locale_snapshot));
  if (!app->brand_icons_valid()) {
    return static_cast<int>(ExitCode::kBrandIconMissing);
  }
  if (!app->new_tab_strings_valid()) {
    return static_cast<int>(ExitCode::kNewTabStringsMissing);
  }
  if (!app->mdv_strings_valid()) {
    return static_cast<int>(ExitCode::kMdvStringsMissing);
  }
  if (!app->page_markdown_strings_valid()) {
    return static_cast<int>(ExitCode::kPageMarkdownStringsMissing);
  }
  if (!app->cast_strings_valid()) {
    return static_cast<int>(ExitCode::kCastStringsMissing);
  }
  if (!CefInitialize(main_args, settings, app, sandbox_info)) {
    const int cef_exit_code = CefGetExitCode();
    return cef_exit_code == 0 ? static_cast<int>(ExitCode::kCefInitializeFailed)
                              : cef_exit_code;
  }

  CefRunMessageLoop();
  CefShutdown();
  return static_cast<int>(ExitCode::kSuccess);
}

}  // namespace crayon::browser::cef_shell::process
