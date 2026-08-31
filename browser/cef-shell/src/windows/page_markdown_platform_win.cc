#include "windows/page_markdown_platform_win.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace crayon::browser::cef_shell::windows {

bool CopyMarkdownToClipboard(const std::string &markdown) {
  if (markdown.empty()) return false;
  const int wide_length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, markdown.data(),
                          static_cast<int>(markdown.size()), nullptr, 0);
  if (wide_length <= 0 || !OpenClipboard(nullptr)) return false;
  const SIZE_T bytes = (static_cast<SIZE_T>(wide_length) + 1) * sizeof(wchar_t);
  HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!storage) {
    CloseClipboard();
    return false;
  }
  auto *value = static_cast<wchar_t *>(GlobalLock(storage));
  const bool converted =
      value &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, markdown.data(),
                          static_cast<int>(markdown.size()), value,
                          wide_length) == wide_length;
  if (value) {
    value[wide_length] = L'\0';
    GlobalUnlock(storage);
  }
  bool copied = false;
  if (converted && EmptyClipboard()) {
    copied = SetClipboardData(CF_UNICODETEXT, storage) != nullptr;
  }
  if (!copied) GlobalFree(storage);
  CloseClipboard();
  return copied;
}

}  // namespace crayon::browser::cef_shell::windows
