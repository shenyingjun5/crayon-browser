#include "browser/window/alloy_downloads.h"

#include <limits>
#include <utility>

namespace crayon::browser::cef_shell::window {

namespace {

class ScopedControlCall final {
 public:
  explicit ScopedControlCall(bool *in_progress) : in_progress_(in_progress) {
    *in_progress_ = true;
  }
  ~ScopedControlCall() { *in_progress_ = false; }
  ScopedControlCall(const ScopedControlCall &) = delete;
  ScopedControlCall &operator=(const ScopedControlCall &) = delete;

 private:
  bool *in_progress_;
};

} // namespace

AlloyDownloads::AlloyDownloads(
    std::string verified_directory,
    browser_downloads::PathExistsPredicate path_exists, Callbacks callbacks)
    : directory_(std::move(verified_directory)), path_exists_(path_exists),
      callbacks_(std::move(callbacks)) {}

permission::DownloadStartDecision
AlloyDownloads::OnDownloadStarting(std::uint64_t download_id,
                                   const std::string &suggested_name,
                                   const std::string &source_url) {
  static_cast<void>(source_url);
  if (!active_ || download_id == 0 || entries_.count(download_id) != 0) {
    return {};
  }
  const auto file_name =
      browser_downloads::SanitizeDownloadFileName(suggested_name);
  const auto path = file_name ? browser_downloads::ResolveUniqueDownloadPath(
                                    directory_, *file_name, path_exists_)
                              : std::nullopt;
  if (!active_ || !file_name || !path)
    return {};
  auto item = browser_downloads::DownloadItem::Create(download_id, *file_name);
  const auto state = item.state();
  if (next_generation_ == 0)
    return {};
  const std::uint64_t generation = next_generation_;
  next_generation_ = generation == (std::numeric_limits<std::uint64_t>::max)()
                         ? 0
                         : generation + 1;
  auto [found, inserted] =
      entries_.emplace(download_id, Entry{std::move(item), *path, generation});
  if (!inserted || !Project(download_id)) {
    entries_.erase(download_id);
    return {};
  }
  return {state == browser_downloads::DownloadState::kPendingDangerConfirm
              ? permission::DownloadStartKind::kPending
              : permission::DownloadStartKind::kAccept,
          state == browser_downloads::DownloadState::kPendingDangerConfirm
              ? std::string{}
              : found->second.target_path,
          generation};
}

void AlloyDownloads::OnDownloadProgress(
    const permission::DownloadUpdate &update) {
  if (!active_)
    return;
  auto found = entries_.find(update.download_id);
  if (found == entries_.end() || found->second.generation != update.generation)
    return;
  auto &item = found->second.item;
  if (update.cancelled) {
    static_cast<void>(item.Cancel());
  } else if (update.complete) {
    if (item.state() == browser_downloads::DownloadState::kPaused) {
      static_cast<void>(item.Resume());
    }
    if (item.state() == browser_downloads::DownloadState::kInProgress) {
      if (!item.OnProgress(update.received_bytes, update.total_bytes) ||
          !item.Complete()) {
        static_cast<void>(item.MarkFailed());
      }
    }
  } else if (update.in_progress &&
             item.state() == browser_downloads::DownloadState::kInProgress) {
    if (!item.OnProgress(update.received_bytes, update.total_bytes)) {
      static_cast<void>(item.MarkFailed());
    }
  } else if (update.in_progress &&
             item.state() == browser_downloads::DownloadState::kPaused) {
    // CEF may deliver one already-posted progress callback after Pause().
    // Keep the user-visible paused state until Resume() is acknowledged.
  } else if (item.state() == browser_downloads::DownloadState::kInProgress ||
             item.state() == browser_downloads::DownloadState::kPaused) {
    static_cast<void>(item.MarkFailed());
  }
  static_cast<void>(Project(update.download_id));
}

bool AlloyDownloads::ConfirmDangerous(std::uint64_t download_id) {
  auto found = entries_.find(download_id);
  if (!active_ || found == entries_.end() ||
      found->second.item.state() !=
          browser_downloads::DownloadState::kPendingDangerConfirm ||
      !callbacks_.confirm_pending) {
    return false;
  }
  const auto confirm_callback = callbacks_.confirm_pending;
  const std::string target_path = found->second.target_path;
  return ApplyControl(
      download_id, &browser_downloads::DownloadItem::ConfirmDangerous,
      [confirm_callback, target_path](std::uint64_t id) {
        return confirm_callback(id, target_path);
      });
}

bool AlloyDownloads::DiscardDangerous(std::uint64_t download_id) {
  return ApplyControl(download_id,
                      &browser_downloads::DownloadItem::DiscardDangerous,
                      callbacks_.discard_pending);
}

bool AlloyDownloads::Pause(std::uint64_t download_id) {
  return ApplyControl(download_id, &browser_downloads::DownloadItem::Pause,
                      callbacks_.pause);
}

bool AlloyDownloads::Resume(std::uint64_t download_id) {
  return ApplyControl(download_id, &browser_downloads::DownloadItem::Resume,
                      callbacks_.resume);
}

bool AlloyDownloads::Cancel(std::uint64_t download_id) {
  return ApplyControl(download_id, &browser_downloads::DownloadItem::Cancel,
                      callbacks_.cancel);
}

bool AlloyDownloads::ApplyControl(std::uint64_t download_id,
                                  ItemControl control,
                                  const ControlCallback &callback) {
  auto found = entries_.find(download_id);
  if (!active_ || control_in_progress_ || found == entries_.end() || !callback)
    return false;
  const std::uint64_t generation = found->second.generation;
  const browser_downloads::DownloadState original_state =
      found->second.item.state();
  auto preview = found->second.item;
  if (!(preview.*control)())
    return false;
  const ControlCallback callback_copy = callback;
  bool callback_accepted = false;
  {
    ScopedControlCall control_call(&control_in_progress_);
    callback_accepted = callback_copy(download_id);
  }
  if (!callback_accepted || !active_)
    return false;
  found = entries_.find(download_id);
  if (found == entries_.end() || found->second.generation != generation ||
      found->second.item.state() != original_state ||
      !(found->second.item.*control)()) {
    return false;
  }
  return Project(download_id);
}

bool AlloyDownloads::OpenLocation(std::uint64_t download_id) const {
  auto found = entries_.find(download_id);
  if (!active_ || found == entries_.end() ||
      !found->second.item.CanOpenLocation() || !callbacks_.open_location) {
    return false;
  }
  const auto open_location = callbacks_.open_location;
  const std::string target_path = found->second.target_path;
  return open_location(target_path);
}

bool AlloyDownloads::Shutdown() {
  if (!active_)
    return true;
  active_ = false;
  callbacks_ = {};
  entries_.clear();
  shelf_.Shutdown();
  return true;
}

bool AlloyDownloads::Project(std::uint64_t download_id) {
  auto found = entries_.find(download_id);
  if (found == entries_.end())
    return false;
  const auto &item = found->second.item;
  const unsigned percent =
      item.total_bytes() == 0
          ? 0
          : static_cast<unsigned>(
                (static_cast<long double>(item.received_bytes()) * 100.0L) /
                static_cast<long double>(item.total_bytes()));
  browser_downloads_view::DownloadProjection projection{
      download_id, item.target_file_name(), item.state(), percent};
  return shelf_.Find(download_id) ? shelf_.OnDownloadUpdated(projection)
                                  : shelf_.OnDownloadStarted(projection);
}

} // namespace crayon::browser::cef_shell::window
