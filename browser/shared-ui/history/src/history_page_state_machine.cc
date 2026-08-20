#include "crayon/browser_history_view/history_page_state_machine.h"

#include <algorithm>

namespace crayon::browser_history_view {

namespace {

constexpr std::size_t kMaxQueryBytes = 256;

}  // namespace

bool HistoryPageStateMachine::SetEntries(
    std::vector<HistoryProjection> entries) {
  if (!active_ || entries.size() > kMaxVisibleEntries) {
    return false;
  }
  const bool valid = std::all_of(
      entries.begin(), entries.end(),
      [](const HistoryProjection& entry) { return entry.entry_id != 0; });
  if (!valid) {
    return false;
  }
  entries_ = std::move(entries);
  return true;
}

bool HistoryPageStateMachine::SetQuery(std::string query) {
  if (!active_ || query.size() > kMaxQueryBytes) {
    return false;
  }
  query_ = std::move(query);
  return true;
}

void HistoryPageStateMachine::OnHistoryCleared() noexcept {
  entries_.clear();
  query_.clear();
}

void HistoryPageStateMachine::Shutdown() noexcept {
  active_ = false;
  entries_.clear();
  query_.clear();
}

}  // namespace crayon::browser_history_view
