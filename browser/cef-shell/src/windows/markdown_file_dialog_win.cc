#include "windows/markdown_file_dialog_win.h"

#include <commdlg.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "include/internal/cef_win.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::windows {
namespace {

constexpr std::size_t kPathBufferCharacters = 32768;
constexpr wchar_t kMarkdownFilter[] = L"Markdown files (*.md)\0*.md\0\0";

bool IsMarkdownRequest(const std::vector<CefString>& accept_filters) {
  return accept_filters.size() == 1 &&
         accept_filters.front().ToString() == ".md";
}

class ScopedOsModalLoop final {
 public:
  ScopedOsModalLoop() { CefSetOSModalLoop(true); }
  ~ScopedOsModalLoop() { CefSetOSModalLoop(false); }

  ScopedOsModalLoop(const ScopedOsModalLoop&) = delete;
  ScopedOsModalLoop& operator=(const ScopedOsModalLoop&) = delete;
};

}  // namespace

bool HandleMarkdownFileDialog(CefRefPtr<CefBrowser> browser,
                              CefDialogHandler::FileDialogMode mode,
                              const CefString& title,
                              const CefString& default_file_path,
                              const std::vector<CefString>& accept_filters,
                              const std::vector<CefString>& accept_extensions,
                              const std::vector<CefString>& accept_descriptions,
                              CefRefPtr<CefFileDialogCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(accept_extensions);
  static_cast<void>(accept_descriptions);

  if (!browser || !callback || !IsMarkdownRequest(accept_filters) ||
      (mode != FILE_DIALOG_OPEN && mode != FILE_DIALOG_SAVE)) {
    return false;
  }

  HWND owner = browser->GetHost()->GetWindowHandle();
  if (!owner) {
    callback->Cancel();
    return true;
  }

  std::array<wchar_t, kPathBufferCharacters> path{};
  const std::wstring suggested_path = default_file_path.ToWString();
  if (suggested_path.size() >= path.size()) {
    callback->Cancel();
    return true;
  }
  std::copy(suggested_path.begin(), suggested_path.end(), path.begin());

  const std::wstring dialog_title = title.ToWString();
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = kMarkdownFilter;
  dialog.lpstrFile = path.data();
  dialog.nMaxFile = static_cast<DWORD>(path.size());
  dialog.lpstrTitle = dialog_title.empty() ? nullptr : dialog_title.c_str();
  dialog.lpstrDefExt = L"md";
  dialog.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_NOCHANGEDIR |
                 OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT;
  dialog.Flags |=
      mode == FILE_DIALOG_OPEN ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT;

  BOOL accepted = FALSE;
  {
    ScopedOsModalLoop modal_loop;
    accepted = mode == FILE_DIALOG_OPEN ? GetOpenFileNameW(&dialog)
                                        : GetSaveFileNameW(&dialog);
  }
  if (!accepted) {
    callback->Cancel();
    return true;
  }

  callback->Continue({CefString(path.data())});
  return true;
}

}  // namespace crayon::browser::cef_shell::windows
