#pragma once

#include <cstdint>
#include <string>

namespace crayon::browser::cef_shell::permission {

enum class DownloadStartKind { kAccept = 0, kPending, kReject };

struct DownloadStartDecision final {
  DownloadStartKind kind = DownloadStartKind::kReject;
  std::string target_path;
  std::uint64_t generation = 0;
};

struct DownloadUpdate final {
  std::uint64_t download_id = 0;
  std::uint64_t generation = 0;
  std::uint64_t received_bytes = 0;
  std::uint64_t total_bytes = 0;
  bool complete = false;
  bool cancelled = false;
  bool in_progress = false;
};

class CefDownloadObserver {
public:
  virtual ~CefDownloadObserver() = default;
  virtual DownloadStartDecision
  OnDownloadStarting(std::uint64_t download_id,
                     const std::string &suggested_name,
                     const std::string &source_url) = 0;
  virtual void OnDownloadProgress(const DownloadUpdate &update) = 0;
};

} // namespace crayon::browser::cef_shell::permission
