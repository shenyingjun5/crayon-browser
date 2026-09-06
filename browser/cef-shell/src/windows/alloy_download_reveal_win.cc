#include "windows/alloy_download_reveal_win.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <filesystem>

namespace crayon::browser::cef_shell::windows {

bool RevealCompletedDownload(const std::string& verified_directory,
                             const std::string& target_path) {
  if (verified_directory.empty() || target_path.empty()) return false;
  std::error_code error;
  const auto directory =
      std::filesystem::canonical(std::filesystem::u8path(verified_directory),
                                 error);
  if (error) return false;
  const auto target =
      std::filesystem::canonical(std::filesystem::u8path(target_path), error);
  if (error || target.parent_path() != directory ||
      !std::filesystem::is_regular_file(target, error) || error) {
    return false;
  }
  const std::wstring parameters = L"/select,\"" + target.native() + L"\"";
  const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
      nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr,
      SW_SHOWNORMAL));
  return result > 32;
}

}  // namespace crayon::browser::cef_shell::windows
