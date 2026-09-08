#include "browser/window/alloy_history.h"

#include <utility>

namespace crayon::browser::cef_shell::window {

AlloyHistory::AlloyHistory(browser_engine::ProfileId profile_id,
                           bool ephemeral, Callbacks callbacks)
    : profile_id_(std::move(profile_id)), callbacks_(std::move(callbacks)),
      store_(ephemeral) {}

bool AlloyHistory::BeginNavigation(std::uint64_t generation) {
  if (!active_ || generation == 0 || generation <= navigation_generation_) {
    return false;
  }
  navigation_generation_ = generation;
  navigation_pending_ = true;
  return true;
}

AlloyHistoryResult AlloyHistory::CommitNavigation(
    std::uint64_t generation, std::string url, std::string title,
    std::uint64_t visited_at) {
  if (!active_) return AlloyHistoryResult::kInactive;
  if (!navigation_pending_ || generation == 0 ||
      generation != navigation_generation_) {
    return AlloyHistoryResult::kStaleNavigation;
  }
  browser_history::HistoryError error{};
  if (store_.RecordVisit(std::move(url), std::move(title), visited_at, &error) ==
      0) {
    return error == browser_history::HistoryError::kEphemeral
               ? AlloyHistoryResult::kEphemeral
               : AlloyHistoryResult::kInvalidInput;
  }
  navigation_pending_ = false;
  return RefreshAll() ? AlloyHistoryResult::kSuccess
                      : AlloyHistoryResult::kRejected;
}

AlloyHistoryResult AlloyHistory::RecordClosedTab(std::string url,
                                                 std::string title,
                                                 std::uint64_t closed_at) {
  if (!active_) return AlloyHistoryResult::kInactive;
  browser_history::HistoryError error{};
  if (!store_.RecordClosedTab(std::move(url), std::move(title), closed_at,
                              &error)) {
    return error == browser_history::HistoryError::kEphemeral
               ? AlloyHistoryResult::kEphemeral
               : AlloyHistoryResult::kInvalidInput;
  }
  return AlloyHistoryResult::kSuccess;
}

AlloyHistoryResult AlloyHistory::RestoreRecentlyClosed() {
  if (!active_) return AlloyHistoryResult::kInactive;
  const auto closed = store_.RestoreRecentlyClosed();
  if (!closed) return AlloyHistoryResult::kNotFound;
  if (!callbacks_.open_new_tab || !callbacks_.open_new_tab(closed->url)) {
    static_cast<void>(store_.RecordClosedTab(closed->url, closed->title,
                                             closed->closed_at));
    return AlloyHistoryResult::kRejected;
  }
  return AlloyHistoryResult::kSuccess;
}

std::size_t AlloyHistory::DeleteRange(std::uint64_t from, std::uint64_t to) {
  if (!active_) return 0;
  const std::size_t count = store_.DeleteRange(from, to);
  static_cast<void>(RefreshAll());
  return count;
}

std::size_t AlloyHistory::DeleteUrl(const std::string &url) {
  if (!active_) return 0;
  const std::size_t count = store_.DeleteUrl(url);
  static_cast<void>(RefreshAll());
  return count;
}

bool AlloyHistory::ClearAll() {
  if (!active_) return false;
  store_.ClearAll();
  view_.OnHistoryCleared();
  return true;
}

bool AlloyHistory::Search(const std::string &query) {
  if (!active_ || !view_.SetQuery(query)) return false;
  if (query.empty()) return RefreshAll();
  std::vector<browser_history_view::HistoryProjection> projected;
  for (const auto &entry : store_.Search(query)) {
    projected.push_back({entry.id,
                         entry.title.empty() ? entry.url : entry.title,
                         entry.visited_at});
  }
  return view_.SetEntries(std::move(projected));
}

bool AlloyHistory::Import(const std::string &document,
                          browser_history::HistoryCodecError *error) {
  if (!active_ || store_.ephemeral()) return false;
  auto candidate = browser_history::DeserializeHistory(document, error);
  if (!candidate) return false;
  auto previous = std::move(store_);
  store_ = std::move(*candidate);
  if (RefreshAll()) return true;
  store_ = std::move(previous);
  static_cast<void>(RefreshAll());
  return false;
}

std::string AlloyHistory::Export() const {
  return active_ && !store_.ephemeral()
             ? browser_history::SerializeHistory(store_)
             : std::string{};
}

bool AlloyHistory::LoadFromFile(
    const std::string &path, browser_history::HistoryCodecError *error) {
  if (!active_ || store_.ephemeral())
    return false;
  auto candidate = browser_history::LoadHistoryFromFile(path, error);
  if (!candidate || candidate->ephemeral())
    return false;
  auto current = std::move(store_);
  store_ = std::move(*candidate);
  if (!RefreshAll()) {
    store_ = std::move(current);
    static_cast<void>(RefreshAll());
    return false;
  }
  return true;
}

bool AlloyHistory::SaveToFile(
    const std::string &path, browser_history::HistoryCodecError *error) const {
  return active_ && !store_.ephemeral() &&
         browser_history::SaveHistoryToFile(store_, path, error);
}

bool AlloyHistory::Shutdown() {
  if (!active_) return true;
  active_ = false;
  callbacks_ = {};
  view_.Shutdown();
  return true;
}

bool AlloyHistory::RefreshAll() {
  if (!view_.query().empty()) return Search(view_.query());
  std::vector<browser_history::HistoryEntry> entries(store_.entries().begin(),
                                                     store_.entries().end());
  return view_.SetEntries(Project(entries));
}

std::vector<browser_history_view::HistoryProjection>
AlloyHistory::Project(const std::vector<browser_history::HistoryEntry> &entries) {
  std::vector<browser_history_view::HistoryProjection> result;
  const std::size_t begin = entries.size() > browser_history_view::kMaxVisibleEntries
                                ? entries.size() - browser_history_view::kMaxVisibleEntries
                                : 0;
  for (std::size_t index = entries.size(); index > begin; --index) {
    const auto &entry = entries[index - 1];
    result.push_back({entry.id, entry.title.empty() ? entry.url : entry.title,
                      entry.visited_at});
  }
  return result;
}

} // namespace crayon::browser::cef_shell::window
