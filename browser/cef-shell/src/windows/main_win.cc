#include <windows.h>

#include <array>
#include <string>
#include <utility>

#include "include/cef_app.h"
#include "include/cef_version_info.h"
#include "resource_ids.h"
#include "windows/app.h"

namespace {

constexpr std::size_t kProductNameCapacity = 128;

enum class ExitCode : int {
  kSuccess = 0,
  kProductNameMissing = 10,
  kCefInitializeFailed = 20,
};

std::wstring LoadProductName(HINSTANCE instance) {
  std::array<wchar_t, kProductNameCapacity> buffer{};
  const int length =
      LoadStringW(instance, IDS_CRAYON_PRODUCT_NAME, buffer.data(),
                  static_cast<int>(buffer.size()));
  if (length <= 0) {
    return {};
  }
  return std::wstring(buffer.data(), static_cast<std::size_t>(length));
}

int RunBrowser(HINSTANCE instance) {
  CefMainArgs main_args(instance);
  const int child_exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
  if (child_exit_code >= 0) {
    return child_exit_code;
  }

  std::wstring product_name = LoadProductName(instance);
  if (product_name.empty()) {
    return static_cast<int>(ExitCode::kProductNameMissing);
  }

  CefSettings settings;
  settings.no_sandbox = true;
  settings.log_severity = LOGSEVERITY_DISABLE;
  CefRefPtr<crayon::browser::cef_shell::BrowserApp> app(
      new crayon::browser::cef_shell::BrowserApp(std::move(product_name)));
  if (!CefInitialize(main_args, settings, app, nullptr)) {
    const int cef_exit_code = CefGetExitCode();
    return cef_exit_code == 0 ? static_cast<int>(ExitCode::kCefInitializeFailed)
                              : cef_exit_code;
  }

  CefRunMessageLoop();
  CefShutdown();
  return static_cast<int>(ExitCode::kSuccess);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous_instance,
                      wchar_t* command_line, int show_command) {
  UNREFERENCED_PARAMETER(previous_instance);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);
  return RunBrowser(instance);
}
