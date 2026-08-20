#include "crayon/browser_navigation/navigation_controller.h"

namespace crayon::browser_navigation {

NavigationController::TabState* NavigationController::FindTabMutable(
    const std::string& tab_id) noexcept {
  if (!active_) {
    return nullptr;
  }
  const auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? nullptr : &it->second;
}

void NavigationController::OnNavigationStarted(const std::string& tab_id,
                                               std::uint64_t navigation_id) {
  auto* tab = FindTabMutable(tab_id);
  if (!tab || navigation_id == 0) {
    return;
  }
  tab->navigation_id = navigation_id;
  tab->is_loading = true;
}

void NavigationController::OnNavigationCommitted(const std::string& tab_id,
                                                 std::uint64_t navigation_id) {
  auto* tab = FindTabMutable(tab_id);
  if (!tab || tab->navigation_id != navigation_id) {
    return;
  }
  // Still loading until Completed or Failed.
}

void NavigationController::OnNavigationCompleted(const std::string& tab_id,
                                                 std::uint64_t navigation_id) {
  auto* tab = FindTabMutable(tab_id);
  if (!tab || tab->navigation_id != navigation_id) {
    return;
  }
  tab->is_loading = false;
}

void NavigationController::OnNavigationFailed(const std::string& tab_id,
                                              std::uint64_t navigation_id) {
  auto* tab = FindTabMutable(tab_id);
  if (!tab || tab->navigation_id != navigation_id) {
    return;
  }
  tab->is_loading = false;
}

void NavigationController::SetCanGoBack(const std::string& tab_id,
                                        bool can) noexcept {
  auto* tab = FindTabMutable(tab_id);
  if (tab) {
    tab->can_go_back = can;
  }
}

void NavigationController::SetCanGoForward(const std::string& tab_id,
                                           bool can) noexcept {
  auto* tab = FindTabMutable(tab_id);
  if (tab) {
    tab->can_go_forward = can;
  }
}

bool NavigationController::GoBack(const std::string& tab_id) noexcept {
  auto* tab = FindTabMutable(tab_id);
  return tab && tab->can_go_back;
}

bool NavigationController::GoForward(const std::string& tab_id) noexcept {
  auto* tab = FindTabMutable(tab_id);
  return tab && tab->can_go_forward;
}

bool NavigationController::Reload(const std::string& tab_id) noexcept {
  auto* tab = FindTabMutable(tab_id);
  return tab && tab->navigation_id != 0;
}

bool NavigationController::Stop(const std::string& tab_id) noexcept {
  auto* tab = FindTabMutable(tab_id);
  return tab && tab->is_loading;
}

bool NavigationController::IsLoading(const std::string& tab_id) const noexcept {
  const auto* tab = FindTab(tab_id);
  return tab && tab->is_loading;
}

bool NavigationController::CanGoBack(const std::string& tab_id) const noexcept {
  const auto* tab = FindTab(tab_id);
  return tab && tab->can_go_back;
}

bool NavigationController::CanGoForward(
    const std::string& tab_id) const noexcept {
  const auto* tab = FindTab(tab_id);
  return tab && tab->can_go_forward;
}

bool NavigationController::CanReload(const std::string& tab_id) const noexcept {
  const auto* tab = FindTab(tab_id);
  return tab && tab->navigation_id != 0;
}

bool NavigationController::CanStop(const std::string& tab_id) const noexcept {
  const auto* tab = FindTab(tab_id);
  return tab && tab->is_loading;
}

std::uint64_t NavigationController::CurrentNavigationId(
    const std::string& tab_id) const noexcept {
  const auto* tab = FindTab(tab_id);
  return tab ? tab->navigation_id : 0;
}

const NavigationController::TabState* NavigationController::FindTab(
    const std::string& tab_id) const noexcept {
  if (!active_) {
    return nullptr;
  }
  const auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? nullptr : &it->second;
}

void NavigationController::OnTabCreated(const std::string& tab_id) {
  if (!active_ || tab_id.empty() || tabs_.count(tab_id) != 0) {
    return;
  }
  tabs_.emplace(tab_id, TabState{});
}

void NavigationController::OnTabClosed(const std::string& tab_id) noexcept {
  tabs_.erase(tab_id);
}

void NavigationController::Shutdown() noexcept {
  active_ = false;
  tabs_.clear();
}

}  // namespace crayon::browser_navigation
