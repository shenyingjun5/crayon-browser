#pragma once

#include <string>

namespace crayon::browser::cef_shell::windows {

// Writes only an explicit user-requested Markdown value to CF_UNICODETEXT.
// The value is not logged or persisted by this adapter.
bool CopyMarkdownToClipboard(const std::string &markdown);

}  // namespace crayon::browser::cef_shell::windows
