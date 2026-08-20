#include "crayon/browser_downloads/download_item.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace crayon::browser_downloads {

namespace {

/// Closed set of executable/script extensions treated as dangerous.
constexpr std::string_view kDangerousExtensions[] = {
    ".exe", ".dll", ".bat", ".cmd", ".com", ".scr", ".pif",
    ".msi", ".ps1", ".vbs", ".js",  ".jar", ".sh",  ".app",
};

std::string_view FinalExtensionOf(const std::string& file_name) noexcept {
  const std::size_t slash = file_name.find_last_of("/\\");
  const std::string_view base =
      slash == std::string::npos
          ? std::string_view(file_name)
          : std::string_view(file_name).substr(slash + 1);
  const std::size_t dot = base.find_last_of('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= base.size()) {
    return {};
  }
  return base.substr(dot);
}

bool EqualsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto lc = static_cast<char>(
        std::tolower(static_cast<unsigned char>(lhs[i])));
    const auto rc = static_cast<char>(
        std::tolower(static_cast<unsigned char>(rhs[i])));
    if (lc != rc) {
      return false;
    }
  }
  return true;
}

}  // namespace

DownloadDanger ClassifyDownloadDanger(const std::string& file_name) noexcept {
  const std::string_view ext = FinalExtensionOf(file_name);
  if (ext.empty()) {
    return DownloadDanger::kSafe;
  }
  for (const std::string_view dangerous : kDangerousExtensions) {
    if (EqualsIgnoreAsciiCase(ext, dangerous)) {
      return DownloadDanger::kDangerous;
    }
  }
  return DownloadDanger::kSafe;
}

DownloadItem DownloadItem::Create(std::uint64_t download_id,
                                  std::string target_file_name) {
  const DownloadState initial =
      ClassifyDownloadDanger(target_file_name) == DownloadDanger::kDangerous
          ? DownloadState::kPendingDangerConfirm
          : DownloadState::kInProgress;
  return DownloadItem(download_id, std::move(target_file_name), initial);
}

DownloadItem::DownloadItem(std::uint64_t download_id,
                           std::string target_file_name,
                           DownloadState initial_state)
    : download_id_(download_id),
      target_file_name_(std::move(target_file_name)),
      state_(initial_state) {}

bool DownloadItem::ConfirmDangerous() noexcept {
  if (state_ != DownloadState::kPendingDangerConfirm) {
    return false;
  }
  state_ = DownloadState::kInProgress;
  return true;
}

bool DownloadItem::DiscardDangerous() noexcept {
  if (state_ != DownloadState::kPendingDangerConfirm) {
    return false;
  }
  state_ = DownloadState::kCancelled;
  return true;
}

bool DownloadItem::OnProgress(std::uint64_t received_bytes,
                              std::uint64_t total_bytes) noexcept {
  if (state_ != DownloadState::kInProgress) {
    return false;
  }
  if (total_bytes != 0 && received_bytes > total_bytes) {
    return false;
  }
  received_bytes_ = received_bytes;
  total_bytes_ = total_bytes;
  return true;
}

bool DownloadItem::Pause() noexcept {
  if (state_ != DownloadState::kInProgress) {
    return false;
  }
  state_ = DownloadState::kPaused;
  return true;
}

bool DownloadItem::Resume() noexcept {
  if (state_ != DownloadState::kPaused) {
    return false;
  }
  state_ = DownloadState::kInProgress;
  return true;
}

bool DownloadItem::Cancel() noexcept {
  switch (state_) {
    case DownloadState::kPendingDangerConfirm:
    case DownloadState::kInProgress:
    case DownloadState::kPaused:
    case DownloadState::kFailed:
      state_ = DownloadState::kCancelled;
      return true;
    case DownloadState::kCompleted:
    case DownloadState::kCancelled:
      return false;
  }
  return false;
}

bool DownloadItem::MarkFailed() noexcept {
  if (state_ != DownloadState::kInProgress &&
      state_ != DownloadState::kPaused) {
    return false;
  }
  state_ = DownloadState::kFailed;
  return true;
}

bool DownloadItem::Retry() noexcept {
  if (state_ != DownloadState::kFailed) {
    return false;
  }
  received_bytes_ = 0;
  state_ = DownloadState::kInProgress;
  return true;
}

bool DownloadItem::Complete() noexcept {
  if (state_ != DownloadState::kInProgress) {
    return false;
  }
  if (total_bytes_ != 0 && received_bytes_ != total_bytes_) {
    return false;
  }
  state_ = DownloadState::kCompleted;
  return true;
}

bool DownloadItem::CanOpenItem() const noexcept {
  return state_ == DownloadState::kCompleted;
}

bool DownloadItem::CanOpenLocation() const noexcept {
  return state_ == DownloadState::kCompleted;
}

}  // namespace crayon::browser_downloads
