#include "macos/alloy_session_restore_mac.h"

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "crayon/browser_session/session_restore.h"
#include "crayon/browser_session/session_snapshot.h"

namespace crayon::browser::cef_shell::macos {
namespace {

using window::AlloySessionFileResult;

constexpr int kTemporaryCreateAttempts = 8;
constexpr int kMaximumInterruptedSyscalls = 64;
constexpr char kTemporaryPrefix[] = ".crayon-session-tmp-";

class FileDescriptor final {
public:
  explicit FileDescriptor(int value = -1) : value_(value) {}
  ~FileDescriptor() {
    if (value_ >= 0)
      static_cast<void>(close(value_));
  }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  FileDescriptor(FileDescriptor &&other) noexcept
      : value_(std::exchange(other.value_, -1)) {}
  FileDescriptor &operator=(FileDescriptor &&other) noexcept {
    if (this != &other) {
      static_cast<void>(Close());
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }

  int get() const { return value_; }
  bool Close() {
    if (value_ < 0)
      return true;
    const int value = value_;
    value_ = -1;
    return close(value) == 0;
  }

private:
  int value_;
};

struct CheckedPath final {
  std::string parent;
  std::string basename;
};

void SetResult(AlloySessionFileResult *out, AlloySessionFileResult value) {
  if (out)
    *out = value;
}

std::optional<CheckedPath> CheckPath(const std::string &path) {
  if (path.empty() || path.find('\0') != std::string::npos ||
      path.front() != '/' || path.size() >= PATH_MAX) {
    return std::nullopt;
  }
  const std::size_t separator = path.rfind('/');
  if (separator == std::string::npos || separator + 1 >= path.size())
    return std::nullopt;
  const std::string basename = path.substr(separator + 1);
  if (basename == "." || basename == "..")
    return std::nullopt;
  const std::string parent = separator == 0 ? "/" : path.substr(0, separator);
  return CheckedPath{parent, basename};
}

FileDescriptor OpenParent(const CheckedPath &path) {
  return FileDescriptor(open(path.parent.c_str(),
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
}

std::string TemporaryBasename() {
  std::uint8_t random[16]{};
  arc4random_buf(random, sizeof(random));
  static constexpr char kHex[] = "0123456789abcdef";
  std::string temporary = kTemporaryPrefix;
  temporary.reserve(sizeof(kTemporaryPrefix) - 1 + sizeof(random) * 2);
  for (const std::uint8_t byte : random) {
    temporary.push_back(kHex[byte >> 4]);
    temporary.push_back(kHex[byte & 0x0f]);
  }
  return temporary;
}

bool WriteAll(int file, const std::string &contents) {
  std::size_t offset = 0;
  int interrupted = 0;
  while (offset < contents.size()) {
    const ssize_t written =
        write(file, contents.data() + offset, contents.size() - offset);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      if (++interrupted <= kMaximumInterruptedSyscalls)
        continue;
    }
    return false;
  }
  return true;
}

bool ReadAll(int file, std::string *contents) {
  std::size_t offset = 0;
  int interrupted = 0;
  while (offset < contents->size()) {
    const ssize_t count =
        read(file, contents->data() + offset, contents->size() - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) {
      if (++interrupted <= kMaximumInterruptedSyscalls)
        continue;
    }
    return false;
  }
  return true;
}

bool Synchronize(int file) {
  for (int interrupted = 0; interrupted <= kMaximumInterruptedSyscalls;
       ++interrupted) {
    if (fsync(file) == 0)
      return true;
    if (errno != EINTR)
      return false;
  }
  return false;
}

AlloySessionFileResult EncodeFailure(browser_session::SessionSnapshotError error) {
  return error == browser_session::SessionSnapshotError::kTooLarge
             ? AlloySessionFileResult::kTooLarge
             : AlloySessionFileResult::kCorrupt;
}

} // namespace

AlloySessionFileResult AlloySessionRestoreMac::SaveCheckpoint(
    const std::string &utf8_absolute_path,
    const browser_session::SessionProfileSnapshot &snapshot,
    browser_session::WindowKind kind) {
  if (kind == browser_session::WindowKind::kIncognito)
    return AlloySessionFileResult::kSkippedIncognito;
  const auto path = CheckPath(utf8_absolute_path);
  if (!path)
    return AlloySessionFileResult::kInvalidPath;
  browser_session::SessionSnapshotError encode_error{};
  const auto encoded = browser_session::EncodeSessionSnapshotV2(snapshot, &encode_error);
  if (!encoded)
    return EncodeFailure(encode_error);

  FileDescriptor parent = OpenParent(*path);
  if (parent.get() < 0)
    return AlloySessionFileResult::kIoError;
  std::string temporary;
  FileDescriptor file;
  for (int attempt = 0; attempt < kTemporaryCreateAttempts; ++attempt) {
    temporary = TemporaryBasename();
    file = FileDescriptor(openat(parent.get(), temporary.c_str(),
                                 O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                 0600));
    if (file.get() >= 0)
      break;
    if (errno != EEXIST)
      return AlloySessionFileResult::kIoError;
  }
  if (file.get() < 0)
    return AlloySessionFileResult::kIoError;
  if (!WriteAll(file.get(), *encoded) || !Synchronize(file.get()) || !file.Close()) {
    static_cast<void>(unlinkat(parent.get(), temporary.c_str(), 0));
    return AlloySessionFileResult::kIoError;
  }
  if (renameat(parent.get(), temporary.c_str(), parent.get(),
               path->basename.c_str()) != 0) {
    static_cast<void>(unlinkat(parent.get(), temporary.c_str(), 0));
    return AlloySessionFileResult::kIoError;
  }
  if (!Synchronize(parent.get()))
    return AlloySessionFileResult::kIoError;
  return AlloySessionFileResult::kSuccess;
}

std::optional<browser_session::SessionProfileSnapshot>
AlloySessionRestoreMac::LoadCheckpoint(const std::string &utf8_absolute_path,
                                       const std::string &profile_id,
                                       AlloySessionFileResult *result) {
  SetResult(result, AlloySessionFileResult::kIoError);
  const auto path = CheckPath(utf8_absolute_path);
  if (!path || !browser_session::SessionRestoreCoordinator::IsValidId(profile_id)) {
    SetResult(result, AlloySessionFileResult::kInvalidPath);
    return std::nullopt;
  }
  FileDescriptor parent = OpenParent(*path);
  if (parent.get() < 0) {
    SetResult(result, errno == ENOENT ? AlloySessionFileResult::kNotFound
                                      : AlloySessionFileResult::kIoError);
    return std::nullopt;
  }
  FileDescriptor file(openat(parent.get(), path->basename.c_str(),
                             O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (file.get() < 0) {
    SetResult(result, errno == ENOENT ? AlloySessionFileResult::kNotFound
                                      : AlloySessionFileResult::kIoError);
    return std::nullopt;
  }
  struct stat metadata {};
  if (fstat(file.get(), &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
    SetResult(result, AlloySessionFileResult::kIoError);
    return std::nullopt;
  }
  if (metadata.st_size > static_cast<off_t>(browser_session::kMaxSessionBytes)) {
    SetResult(result, AlloySessionFileResult::kTooLarge);
    return std::nullopt;
  }
  if (metadata.st_size <= 0) {
    SetResult(result, AlloySessionFileResult::kCorrupt);
    return std::nullopt;
  }
  std::string encoded(static_cast<std::size_t>(metadata.st_size), '\0');
  if (!ReadAll(file.get(), &encoded) || !file.Close()) {
    SetResult(result, AlloySessionFileResult::kIoError);
    return std::nullopt;
  }
  browser_session::SessionSnapshotError decode_error{};
  const auto decoded = browser_session::DecodeSessionSnapshot(encoded, &decode_error);
  if (!decoded) {
    SetResult(result, EncodeFailure(decode_error));
    return std::nullopt;
  }
  if (decoded->profile_id != profile_id) {
    SetResult(result, AlloySessionFileResult::kProfileMismatch);
    return std::nullopt;
  }
  SetResult(result, AlloySessionFileResult::kSuccess);
  return decoded;
}

} // namespace crayon::browser::cef_shell::macos
