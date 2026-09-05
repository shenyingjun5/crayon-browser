#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "browser/window/alloy_session_restore.h"

namespace {

using crayon::browser::cef_shell::window::AlloySessionFileResult;
using crayon::browser::cef_shell::window::AlloySessionRestore;
using crayon::browser_session::SessionProfileSnapshot;
using crayon::browser_session::SessionTabSnapshot;
using crayon::browser_session::SessionWindowSnapshot;
using crayon::browser_session::WindowKind;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

bool WriteCorrupt(const std::wstring &path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  constexpr char payload[] = "not-a-session";
  DWORD written = 0;
  const bool ok = WriteFile(file, payload, sizeof(payload) - 1, &written,
                            nullptr) != FALSE &&
                  written == sizeof(payload) - 1;
  return CloseHandle(file) != FALSE && ok;
}

bool AtomicFileContract() {
  wchar_t temp_root[MAX_PATH]{};
  CHECK(GetTempPathW(MAX_PATH, temp_root) != 0);
  const std::wstring directory =
      std::wstring(temp_root) + L"crayon-alloy-session-" +
      std::to_wstring(GetCurrentProcessId());
  CHECK(CreateDirectoryW(directory.c_str(), nullptr) != FALSE ||
        GetLastError() == ERROR_ALREADY_EXISTS);
  const std::wstring path = directory + L"\\session.snapshot";
  const std::wstring private_path = directory + L"\\private.snapshot";
  static_cast<void>(DeleteFileW(path.c_str()));
  static_cast<void>(DeleteFileW(private_path.c_str()));

  const SessionProfileSnapshot snapshot{
      "profile-a",
      {SessionWindowSnapshot{
          "window-a",
          {SessionTabSnapshot{"https://example.test/", true, true,
                              std::string("group-a")}},
          0}}};
  CHECK(AlloySessionRestore::SaveCheckpoint(path, snapshot,
                                            WindowKind::kRegular) ==
        AlloySessionFileResult::kSuccess);
  AlloySessionFileResult result = AlloySessionFileResult::kIoError;
  auto loaded =
      AlloySessionRestore::LoadCheckpoint(path, "profile-a", &result);
  CHECK(loaded && result == AlloySessionFileResult::kSuccess);
  CHECK(loaded->windows.front().tabs.front().muted);
  CHECK(!AlloySessionRestore::LoadCheckpoint(path, "profile-b", &result));
  CHECK(result == AlloySessionFileResult::kProfileMismatch);

  CHECK(WriteCorrupt(path));
  CHECK(!AlloySessionRestore::LoadCheckpoint(path, "profile-a", &result));
  CHECK(result == AlloySessionFileResult::kCorrupt);
  CHECK(AlloySessionRestore::SaveCheckpoint(path, snapshot,
                                            WindowKind::kRegular) ==
        AlloySessionFileResult::kSuccess);
  CHECK(AlloySessionRestore::SaveCheckpoint(private_path, snapshot,
                                            WindowKind::kIncognito) ==
        AlloySessionFileResult::kSkippedIncognito);
  CHECK(GetFileAttributesW(private_path.c_str()) == INVALID_FILE_ATTRIBUTES);

  CHECK(DeleteFileW(path.c_str()) != FALSE);
  CHECK(RemoveDirectoryW(directory.c_str()) != FALSE);
  return true;
}

} // namespace

int main() {
  if (!AtomicFileContract()) {
    return EXIT_FAILURE;
  }
  std::cout << "alloy_session_restore_win_test passed\n";
  return EXIT_SUCCESS;
}
