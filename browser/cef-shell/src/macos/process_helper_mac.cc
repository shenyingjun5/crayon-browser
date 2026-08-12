#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

namespace {

constexpr int kSandboxInitializeFailed = 10;
constexpr int kFrameworkLoadFailed = 11;

}  // namespace

int main(int argc, char* argv[]) {
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) {
    return kSandboxInitializeFailed;
  }
#endif

  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    return kFrameworkLoadFailed;
  }

  CefMainArgs main_args(argc, argv);
  return CefExecuteProcess(main_args, nullptr, nullptr);
}
