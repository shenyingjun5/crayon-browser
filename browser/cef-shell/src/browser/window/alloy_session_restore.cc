#include "browser/window/alloy_session_restore.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

namespace crayon::browser::cef_shell::window {
namespace {

std::atomic<std::uint64_t> g_temp_sequence{1};

void SetResult(AlloySessionFileResult *out, AlloySessionFileResult value) {
  if (out) {
    *out = value;
  }
}

bool IsUsablePath(const std::wstring &path) {
  if (path.empty() || path.size() >= 30000) {
    return false;
  }
  const std::size_t separator = path.find_last_of(L"\\/");
  return separator != std::wstring::npos && separator + 1 < path.size();
}

std::wstring TempPath(const std::wstring &path) {
  return path + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(g_temp_sequence.fetch_add(1));
}

bool WriteAll(HANDLE file, const std::string &contents) {
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const DWORD chunk = static_cast<DWORD>(
        (std::min)(contents.size() - offset,
                   static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    if (!WriteFile(file, contents.data() + offset, chunk, &written, nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return FlushFileBuffers(file) != FALSE;
}

} // namespace

AlloySessionFileResult AlloySessionRestore::SaveCheckpoint(
    const std::wstring &path,
    const browser_session::SessionProfileSnapshot &snapshot,
    browser_session::WindowKind kind) {
  if (kind == browser_session::WindowKind::kIncognito) {
    return AlloySessionFileResult::kSkippedIncognito;
  }
  if (!IsUsablePath(path)) {
    return AlloySessionFileResult::kInvalidPath;
  }
  browser_session::SessionSnapshotError encode_error{};
  const auto encoded =
      browser_session::EncodeSessionSnapshotV2(snapshot, &encode_error);
  if (!encoded) {
    return encode_error == browser_session::SessionSnapshotError::kTooLarge
               ? AlloySessionFileResult::kTooLarge
               : AlloySessionFileResult::kCorrupt;
  }

  const std::wstring temporary = TempPath(path);
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return AlloySessionFileResult::kIoError;
  }
  const bool wrote = WriteAll(file, *encoded);
  const bool closed = CloseHandle(file) != FALSE;
  if (!wrote || !closed) {
    static_cast<void>(DeleteFileW(temporary.c_str()));
    return AlloySessionFileResult::kIoError;
  }

  const DWORD attributes = GetFileAttributesW(path.c_str());
  const bool replaced =
      attributes != INVALID_FILE_ATTRIBUTES
          ? ReplaceFileW(path.c_str(), temporary.c_str(), nullptr,
                         REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) !=
                FALSE
          : MoveFileExW(temporary.c_str(), path.c_str(),
                        MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!replaced) {
    static_cast<void>(DeleteFileW(temporary.c_str()));
    return AlloySessionFileResult::kIoError;
  }
  return AlloySessionFileResult::kSuccess;
}

std::optional<browser_session::SessionProfileSnapshot>
AlloySessionRestore::LoadCheckpoint(const std::wstring &path,
                                    const std::string &profile_id,
                                    AlloySessionFileResult *result) {
  SetResult(result, AlloySessionFileResult::kIoError);
  if (!IsUsablePath(path) ||
      !browser_session::SessionRestoreCoordinator::IsValidId(profile_id)) {
    SetResult(result, AlloySessionFileResult::kInvalidPath);
    return std::nullopt;
  }
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    SetResult(result, GetLastError() == ERROR_FILE_NOT_FOUND
                          ? AlloySessionFileResult::kNotFound
                          : AlloySessionFileResult::kIoError);
    return std::nullopt;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      size.QuadPart >
          static_cast<LONGLONG>(browser_session::kMaxSessionBytes)) {
    CloseHandle(file);
    SetResult(result, size.QuadPart > static_cast<LONGLONG>(
                                        browser_session::kMaxSessionBytes)
                          ? AlloySessionFileResult::kTooLarge
                          : AlloySessionFileResult::kCorrupt);
    return std::nullopt;
  }
  std::string encoded(static_cast<std::size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const bool read_ok = ReadFile(file, encoded.data(),
                                static_cast<DWORD>(encoded.size()), &read,
                                nullptr) != FALSE &&
                       read == static_cast<DWORD>(encoded.size());
  const bool close_ok = CloseHandle(file) != FALSE;
  if (!read_ok || !close_ok) {
    return std::nullopt;
  }
  const auto decoded = browser_session::DecodeSessionSnapshot(encoded);
  if (!decoded) {
    SetResult(result, AlloySessionFileResult::kCorrupt);
    return std::nullopt;
  }
  if (decoded->profile_id != profile_id) {
    SetResult(result, AlloySessionFileResult::kProfileMismatch);
    return std::nullopt;
  }
  SetResult(result, AlloySessionFileResult::kSuccess);
  return decoded;
}

} // namespace crayon::browser::cef_shell::window
