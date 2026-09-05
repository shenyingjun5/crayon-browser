#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "browser/permission/download_observer.h"
#include "crayon/browser_downloads/download_item.h"
#include "crayon/browser_downloads/download_path.h"
#include "crayon/browser_downloads_view/download_shelf_state_machine.h"

namespace crayon::browser::cef_shell::window {

class AlloyDownloads final : public permission::CefDownloadObserver {
public:
  struct Callbacks final {
    std::function<bool(std::uint64_t, const std::string &)> confirm_pending;
    std::function<bool(std::uint64_t)> discard_pending;
    std::function<bool(std::uint64_t)> pause;
    std::function<bool(std::uint64_t)> resume;
    std::function<bool(std::uint64_t)> cancel;
    std::function<bool(const std::string &)> open_location;
  };

  AlloyDownloads(std::string verified_directory,
                 browser_downloads::PathExistsPredicate path_exists,
                 Callbacks callbacks);

  permission::DownloadStartDecision
  OnDownloadStarting(std::uint64_t download_id,
                     const std::string &suggested_name,
                     const std::string &source_url) override;
  void OnDownloadProgress(const permission::DownloadUpdate &update) override;

  bool ConfirmDangerous(std::uint64_t download_id);
  bool DiscardDangerous(std::uint64_t download_id);
  bool Pause(std::uint64_t download_id);
  bool Resume(std::uint64_t download_id);
  bool Cancel(std::uint64_t download_id);
  bool OpenLocation(std::uint64_t download_id) const;
  bool Shutdown();

  const browser_downloads_view::DownloadShelfStateMachine &shelf() const {
    return shelf_;
  }

private:
  struct Entry final {
    browser_downloads::DownloadItem item;
    std::string target_path;
    std::uint64_t generation = 0;
  };

  bool Project(std::uint64_t download_id);

  std::string directory_;
  browser_downloads::PathExistsPredicate path_exists_;
  Callbacks callbacks_;
  std::map<std::uint64_t, Entry> entries_;
  browser_downloads_view::DownloadShelfStateMachine shelf_;
  bool active_ = true;
  std::uint64_t next_generation_ = 1;
};

} // namespace crayon::browser::cef_shell::window
