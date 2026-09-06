#pragma once

#include <string>

namespace crayon::browser::cef_shell::windows {

/// Opens Explorer with an existing regular file selected after constraining
/// the canonical target to the canonical verified download directory.
bool RevealCompletedDownload(const std::string& verified_directory,
                             const std::string& target_path);

}  // namespace crayon::browser::cef_shell::windows
