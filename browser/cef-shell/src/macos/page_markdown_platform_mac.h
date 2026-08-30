#pragma once

#include <string>

namespace crayon::browser::cef_shell::macos {

// Writes only an explicit user-requested Markdown value to the system
// pasteboard. The value is neither logged nor persisted by this adapter.
bool CopyMarkdownToPasteboard(const std::string& markdown);

}  // namespace crayon::browser::cef_shell::macos
