#include "crayon/browser_downloads_view/download_shelf_state_machine.h"

#include <algorithm>

namespace crayon::browser_downloads_view {

namespace {

using crayon::browser_downloads::DownloadState;

bool IsValidProjection(const DownloadProjection& projection) noexcept {
  return projection.download_id != 0 && !projection.display_name.empty() &&
         IsValid(projection.state) && projection.percent <= 100;
}

}  // namespace

bool DownloadShelfStateMachine::IsFinished(DownloadState state) noexcept {
  return state == DownloadState::kCompleted ||
         state == DownloadState::kCancelled;
}

bool DownloadShelfStateMachine::OnDownloadStarted(
    const DownloadProjection& projection) {
  if (!active_ || !IsValidProjection(projection)) {
    return false;
  }
  if (Find(projection.download_id) != nullptr ||
      items_.size() >= kMaxVisibleDownloads) {
    return false;
  }
  items_.push_back(projection);
  shelf_open_ = true;
  return true;
}

bool DownloadShelfStateMachine::OnDownloadUpdated(
    const DownloadProjection& projection) {
  if (!active_ || !IsValidProjection(projection)) {
    return false;
  }
  const auto it =
      std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.download_id == projection.download_id;
      });
  if (it == items_.end()) {
    return false;
  }
  *it = projection;
  return true;
}

bool DownloadShelfStateMachine::OnDownloadRemoved(
    std::uint64_t download_id) noexcept {
  if (!active_) {
    return false;
  }
  const auto it =
      std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.download_id == download_id;
      });
  if (it == items_.end()) {
    return false;
  }
  items_.erase(it);
  return true;
}

std::size_t DownloadShelfStateMachine::ClearCompleted() noexcept {
  const std::size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [](const DownloadProjection& item) {
                                return IsFinished(item.state);
                              }),
               items_.end());
  return before - items_.size();
}

const DownloadProjection* DownloadShelfStateMachine::Find(
    std::uint64_t download_id) const noexcept {
  if (!active_) {
    return nullptr;
  }
  const auto it =
      std::find_if(items_.begin(), items_.end(), [&](const auto& item) {
        return item.download_id == download_id;
      });
  return it == items_.end() ? nullptr : &*it;
}

std::size_t DownloadShelfStateMachine::active_count() const noexcept {
  std::size_t count = 0;
  for (const auto& item : items_) {
    if (!IsFinished(item.state)) {
      ++count;
    }
  }
  return count;
}

void DownloadShelfStateMachine::Shutdown() noexcept {
  active_ = false;
  items_.clear();
  shelf_open_ = false;
}

}  // namespace crayon::browser_downloads_view
