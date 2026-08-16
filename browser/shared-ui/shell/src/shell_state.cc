#include "crayon/browser_shell/shell_state.h"

#include <algorithm>
#include <utility>

namespace crayon::browser_shell {
namespace {

int NavigationRank(NavigationState state) noexcept {
  switch (state) {
    case NavigationState::kNone:
      return 0;
    case NavigationState::kStarted:
      return 1;
    case NavigationState::kCommitted:
      return 2;
    case NavigationState::kCompleted:
    case NavigationState::kFailed:
      return 3;
  }
  return -1;
}

bool FocusRequiresTab(FocusArea area) noexcept {
  return area == FocusArea::kTabStrip || area == FocusArea::kPage;
}

NavigationState ToShellNavigationState(
    browser_engine::NavigationEventKind kind) noexcept {
  switch (kind) {
    case browser_engine::NavigationEventKind::kStarted:
      return NavigationState::kStarted;
    case browser_engine::NavigationEventKind::kCommitted:
      return NavigationState::kCommitted;
    case browser_engine::NavigationEventKind::kCompleted:
      return NavigationState::kCompleted;
    case browser_engine::NavigationEventKind::kFailed:
      return NavigationState::kFailed;
  }
  return NavigationState::kNone;
}

}  // namespace

bool ShellState::OnProfileCreated(std::string profile_id) {
  if (!active_ || profile_id.empty() || profiles_.count(profile_id) != 0 ||
      retired_profiles_.count(profile_id) != 0) {
    return false;
  }
  profiles_.insert(std::move(profile_id));
  return true;
}

bool ShellState::OnProfileDestroyed(const std::string& profile_id) {
  if (!active_) {
    return false;
  }
  if (retired_profiles_.count(profile_id) != 0) {
    return true;
  }
  if (profiles_.count(profile_id) == 0) {
    return false;
  }
  const bool has_tabs =
      std::any_of(tabs_.begin(), tabs_.end(), [&profile_id](const auto& item) {
        return item.second.profile_id == profile_id;
      });
  if (has_tabs) {
    return false;
  }
  profiles_.erase(profile_id);
  retired_profiles_.insert(profile_id);
  return true;
}

bool ShellState::OnTabCreated(std::string profile_id, std::string tab_id) {
  if (!active_ || tab_id.empty() || profiles_.count(profile_id) == 0 ||
      tabs_.count(tab_id) != 0 || retired_tabs_.count(tab_id) != 0) {
    return false;
  }
  ShellTabView tab;
  tab.profile_id = std::move(profile_id);
  tab.tab_id = tab_id;
  tabs_.emplace(std::move(tab_id), std::move(tab));
  return true;
}

bool ShellState::OnTabClosed(const std::string& tab_id) {
  if (!active_) {
    return false;
  }
  if (retired_tabs_.count(tab_id) != 0) {
    return true;
  }
  const auto tab = tabs_.find(tab_id);
  if (tab == tabs_.end()) {
    return false;
  }
  tabs_.erase(tab);
  retired_tabs_.insert(tab_id);
  if (focused_tab_id_ == tab_id) {
    focus_area_ = FocusArea::kNone;
    focused_tab_id_.reset();
  }
  InvalidateRestoreTokenForTab(tab_id);
  return true;
}

bool ShellState::OnNavigation(const std::string& tab_id,
                              std::uint64_t navigation_id,
                              NavigationState state, std::string url) {
  if (!active_ || navigation_id == 0 || !IsValid(state) ||
      state == NavigationState::kNone) {
    return false;
  }
  auto tab = tabs_.find(tab_id);
  if (tab == tabs_.end()) {
    return false;
  }
  if (navigation_id < tab->second.navigation_id) {
    return false;
  }
  if (navigation_id > tab->second.navigation_id) {
    if (state != NavigationState::kStarted) {
      return false;
    }
    tab->second.navigation_id = navigation_id;
    tab->second.navigation_state = state;
    tab->second.url = std::move(url);
    return true;
  }

  const int current_rank = NavigationRank(tab->second.navigation_state);
  const int new_rank = NavigationRank(state);
  if (new_rank < current_rank ||
      (current_rank == 3 && state != tab->second.navigation_state)) {
    return false;
  }
  tab->second.navigation_state = state;
  tab->second.url = std::move(url);
  return true;
}

bool ShellState::SetFocus(FocusArea area, std::optional<std::string> tab_id) {
  if (!active_ || !IsValid(area)) {
    return false;
  }
  if (FocusRequiresTab(area)) {
    if (!tab_id.has_value() || !IsLiveTab(*tab_id)) {
      return false;
    }
  } else if (tab_id.has_value()) {
    return false;
  }
  focus_area_ = area;
  focused_tab_id_ = std::move(tab_id);
  return true;
}

std::optional<FocusToken> ShellState::CaptureFocusForRestore() {
  if (!active_ || focus_area_ == FocusArea::kNone ||
      next_focus_generation_ == 0) {
    return std::nullopt;
  }
  restore_token_ =
      FocusToken{next_focus_generation_++, focus_area_, focused_tab_id_};
  return restore_token_;
}

bool ShellState::RestoreFocus(const FocusToken& token) {
  if (!active_ || !restore_token_.has_value() || token.generation == 0 ||
      token.generation != restore_token_->generation ||
      token.area != restore_token_->area ||
      token.tab_id != restore_token_->tab_id) {
    return false;
  }
  if (token.tab_id.has_value() && !IsLiveTab(*token.tab_id)) {
    restore_token_.reset();
    return false;
  }
  focus_area_ = token.area;
  focused_tab_id_ = token.tab_id;
  restore_token_.reset();
  return true;
}

void ShellState::OnCommandAccepted(ShellCommand command, CommandOrigin origin) {
  static_cast<void>(origin);
  if (command == ShellCommand::kFocusOmnibox) {
    SetFocus(FocusArea::kOmnibox, std::nullopt);
  }
}

void ShellState::Shutdown() noexcept {
  active_ = false;
  profiles_.clear();
  tabs_.clear();
  focus_area_ = FocusArea::kNone;
  focused_tab_id_.reset();
  restore_token_.reset();
  next_focus_generation_ = 0;
}

const ShellTabView* ShellState::FindTab(
    const std::string& tab_id) const noexcept {
  const auto tab = tabs_.find(tab_id);
  return tab == tabs_.end() ? nullptr : &tab->second;
}

bool ShellState::IsLiveTab(const std::string& tab_id) const noexcept {
  return tabs_.count(tab_id) != 0;
}

void ShellState::InvalidateRestoreTokenForTab(
    const std::string& tab_id) noexcept {
  if (restore_token_.has_value() && restore_token_->tab_id == tab_id) {
    restore_token_.reset();
  }
}

EngineEventAdapter::~EngineEventAdapter() { Shutdown(); }

void EngineEventAdapter::OnProfileEvent(
    const browser_engine::ProfileEvent& event) {
  if (!state_) {
    return;
  }
  switch (event.kind) {
    case browser_engine::ProfileEventKind::kCreated:
      state_->OnProfileCreated(event.profile_id.value());
      break;
    case browser_engine::ProfileEventKind::kDestroyed:
      state_->OnProfileDestroyed(event.profile_id.value());
      break;
  }
}

void EngineEventAdapter::OnTabEvent(const browser_engine::TabEvent& event) {
  if (!state_) {
    return;
  }
  switch (event.kind) {
    case browser_engine::TabEventKind::kCreated:
      state_->OnTabCreated(event.profile_id.value(), event.tab_id.value());
      break;
    case browser_engine::TabEventKind::kClosed:
      state_->OnTabClosed(event.tab_id.value());
      break;
  }
}

void EngineEventAdapter::OnNavigationEvent(
    const browser_engine::NavigationEvent& event) {
  if (!state_) {
    return;
  }
  state_->OnNavigation(event.tab_id.value(), event.navigation_id.value(),
                       ToShellNavigationState(event.kind), event.url.value());
}

void EngineEventAdapter::OnPermissionRequest(
    const browser_engine::PermissionRequest& request) {
  static_cast<void>(request);
}

void EngineEventAdapter::OnTrustedInput(
    const browser_engine::TrustedInputFact& fact) {
  static_cast<void>(fact);
}

void EngineEventAdapter::OnObservation(
    const browser_engine::ObservationEvent& event) {
  static_cast<void>(event);
}

void EngineEventAdapter::Shutdown() noexcept {
  if (!state_) {
    return;
  }
  state_->Shutdown();
  state_ = nullptr;
}

}  // namespace crayon::browser_shell
