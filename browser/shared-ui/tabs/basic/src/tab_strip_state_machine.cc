#include "crayon/browser_tabs/tab_strip_state_machine.h"

#include <algorithm>

namespace crayon::browser_tabs {

bool TabStripStateMachine::AddTab(std::string tab_id) {
  if (!active_ || tab_id.empty() || tabs_.size() >= kMaxTabCount ||
      FindTabIndex(tab_id).has_value()) {
    return false;
  }
  tabs_.push_back(std::move(tab_id));
  active_index_ = tabs_.size() - 1;
  return true;
}

bool TabStripStateMachine::CloseTab(const std::string& tab_id) {
  if (!active_) {
    return false;
  }
  const auto index = FindTabIndex(tab_id);
  if (!index.has_value()) {
    return false;
  }
  PushClosed(tab_id);
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*index));
  ClampActiveIndex();
  return true;
}

bool TabStripStateMachine::ActivateTab(const std::string& tab_id) {
  if (!active_) {
    return false;
  }
  const auto index = FindTabIndex(tab_id);
  if (!index.has_value()) {
    return false;
  }
  active_index_ = *index;
  return true;
}

bool TabStripStateMachine::SelectNext() noexcept {
  if (!active_ || tabs_.empty() || !active_index_.has_value()) {
    return false;
  }
  active_index_ = (*active_index_ + 1) % tabs_.size();
  return true;
}

bool TabStripStateMachine::SelectPrevious() noexcept {
  if (!active_ || tabs_.empty() || !active_index_.has_value()) {
    return false;
  }
  active_index_ = (*active_index_ + tabs_.size() - 1) % tabs_.size();
  return true;
}

bool TabStripStateMachine::MoveTab(std::size_t from_index,
                                   std::size_t to_index) noexcept {
  if (!active_ || from_index >= tabs_.size() || to_index >= tabs_.size() ||
      from_index == to_index) {
    return false;
  }
  const auto from_it = tabs_.begin() + static_cast<std::ptrdiff_t>(from_index);
  const std::string tab_id = std::move(*from_it);
  tabs_.erase(from_it);
  const auto to_it = tabs_.begin() + static_cast<std::ptrdiff_t>(to_index);
  tabs_.insert(to_it, std::move(tab_id));

  // Adjust active index if it was affected by the move
  if (active_index_.has_value()) {
    std::size_t old_active = *active_index_;
    if (old_active == from_index) {
      active_index_ = to_index;
    } else if (from_index < old_active && to_index >= old_active) {
      active_index_ = old_active - 1;
    } else if (from_index > old_active && to_index <= old_active) {
      active_index_ = old_active + 1;
    }
  }
  return true;
}

bool TabStripStateMachine::RestoreClosed() {
  if (!active_ || closed_stack_.empty()) {
    return false;
  }
  ClosedTabInfo info = std::move(closed_stack_.front());
  closed_stack_.pop_front();

  if (tabs_.size() >= kMaxTabCount || FindTabIndex(info.tab_id).has_value()) {
    return false;
  }
  tabs_.push_back(std::move(info.tab_id));
  active_index_ = tabs_.size() - 1;
  return true;
}

void TabStripStateMachine::OnTabCreated(std::string tab_id) {
  if (!active_ || tab_id.empty() || tabs_.size() >= kMaxTabCount ||
      FindTabIndex(tab_id).has_value()) {
    return;
  }
  tabs_.push_back(std::move(tab_id));
  if (!active_index_.has_value()) {
    active_index_ = 0;
  }
}

void TabStripStateMachine::OnTabClosed(const std::string& tab_id) {
  const auto index = FindTabIndex(tab_id);
  if (!index.has_value()) {
    return;
  }
  PushClosed(tab_id);
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(*index));
  ClampActiveIndex();
}

std::optional<std::size_t> TabStripStateMachine::active_index() const noexcept {
  if (!active_ || !active_index_.has_value() ||
      *active_index_ >= tabs_.size()) {
    return std::nullopt;
  }
  return active_index_;
}

std::optional<std::string> TabStripStateMachine::active_tab_id() const noexcept {
  const auto idx = active_index();
  if (!idx.has_value()) {
    return std::nullopt;
  }
  return tabs_[*idx];
}

std::optional<std::size_t> TabStripStateMachine::FindTabIndex(
    const std::string& tab_id) const noexcept {
  const auto it = std::find(tabs_.begin(), tabs_.end(), tab_id);
  if (it == tabs_.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(it - tabs_.begin());
}

void TabStripStateMachine::Shutdown() noexcept {
  active_ = false;
  tabs_.clear();
  active_index_.reset();
  closed_stack_.clear();
}

void TabStripStateMachine::ClampActiveIndex() noexcept {
  if (tabs_.empty()) {
    active_index_.reset();
    return;
  }
  if (!active_index_.has_value()) {
    active_index_ = 0;
    return;
  }
  if (*active_index_ >= tabs_.size()) {
    active_index_ = tabs_.size() - 1;
  }
}

void TabStripStateMachine::PushClosed(const std::string& tab_id) {
  if (closed_stack_.size() >= kMaxRestorableTabs) {
    closed_stack_.pop_back();
  }
  closed_stack_.push_front(ClosedTabInfo{tab_id, ""});
}

}  // namespace crayon::browser_tabs
