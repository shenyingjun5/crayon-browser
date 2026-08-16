#include "process/windows/bootstrap_entry.h"

#include <array>
#include <string>
#include <utility>

#include "include/cef_app.h"
#include "include/cef_version_info.h"
#include "resource_ids.h"
#include "windows/app.h"
#include "windows/new_tab_scheme_handler.h"

namespace crayon::browser::cef_shell::process {
namespace {

constexpr std::size_t kProductNameCapacity = 128;

class ChildProcessApp final : public CefApp {
 public:
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override {
    RegisterCrayonScheme(registrar);
  }

 private:
  IMPLEMENT_REFCOUNTING(ChildProcessApp);
  DISALLOW_COPY_AND_ASSIGN(ChildProcessApp);
};

enum class ExitCode : int {
  kSuccess = 0,
  kBootstrapInstanceMissing = 10,
  kSandboxInfoMissing = 11,
  kClientModuleMissing = 12,
  kProductNameMissing = 13,
  kBrandIconMissing = 14,
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

std::wstring LoadProductName(HINSTANCE client_module) {
  std::array<wchar_t, kProductNameCapacity> buffer{};
  const int length =
      LoadStringW(client_module, IDS_CRAYON_PRODUCT_NAME, buffer.data(),
                  static_cast<int>(buffer.size()));
  if (length <= 0) {
    return {};
  }
  return std::wstring(buffer.data(), static_cast<std::size_t>(length));
}

}  // namespace

int RunBrowserProcess(HINSTANCE bootstrap_instance, void* sandbox_info) {
  if (!bootstrap_instance) {
    return static_cast<int>(ExitCode::kBootstrapInstanceMissing);
  }
  if (!sandbox_info) {
    return static_cast<int>(ExitCode::kSandboxInfoMissing);
  }

  CefMainArgs main_args(bootstrap_instance);
  const int child_exit_code = CefExecuteProcess(
      main_args, CefRefPtr<CefApp>(new ChildProcessApp()), sandbox_info);
  if (child_exit_code >= 0) {
    return child_exit_code;
  }

  HINSTANCE client_module = GetClientModule();
  if (!client_module) {
    return static_cast<int>(ExitCode::kClientModuleMissing);
  }
  std::wstring product_name = LoadProductName(client_module);
  if (product_name.empty()) {
    return static_cast<int>(ExitCode::kProductNameMissing);
  }

  CefSettings settings;
  settings.log_severity = LOGSEVERITY_DISABLE;
  CefRefPtr<BrowserApp> app(
      new BrowserApp(client_module, std::move(product_name)));
  if (!app->brand_icons_valid()) {
    return static_cast<int>(ExitCode::kBrandIconMissing);
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
