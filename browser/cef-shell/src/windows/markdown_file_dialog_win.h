#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_MARKDOWN_FILE_DIALOG_WIN_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_MARKDOWN_FILE_DIALOG_WIN_H_

#include <vector>

#include "include/cef_dialog_handler.h"

namespace crayon::browser::cef_shell::windows {

bool HandleMarkdownFileDialog(CefRefPtr<CefBrowser> browser,
                              CefDialogHandler::FileDialogMode mode,
                              const CefString& title,
                              const CefString& default_file_path,
                              const std::vector<CefString>& accept_filters,
                              const std::vector<CefString>& accept_extensions,
                              const std::vector<CefString>& accept_descriptions,
                              CefRefPtr<CefFileDialogCallback> callback);

}  // namespace crayon::browser::cef_shell::windows

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_MARKDOWN_FILE_DIALOG_WIN_H_
