#include "browser/window/tab_model.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace crayon::browser::cef_shell::window {

std::optional<TabId> TabModel::CreateTab() {
  if (tabs_.size() >= kMaximumTabsPerWindow || next_tab_id_ == 0) {
    return std::nullopt;
  }
  const TabId id = next_tab_id_++;
  tabs_.push_back({id,
                   0,
                   TabLifecycle::kCreating,
                   {},
                   false,
                   false,
                   false,
                   kDefaultZoomFactor,
                   0});
  active_tab_ = id;
  return id;
}

bool TabModel::BindBrowser(TabId tab_id, int browser_id) {
  if (browser_id <= 0 || FindByBrowser(browser_id)) {
    return false;
  }
  TabSnapshot *tab = FindMutable(tab_id);
  if (!tab || tab->lifecycle != TabLifecycle::kCreating) {
    return false;
  }
  tab->browser_id = browser_id;
  tab->lifecycle = TabLifecycle::kReady;
  return true;
}

bool TabModel::Activate(TabId tab_id) {
  const TabSnapshot *tab = Find(tab_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  active_tab_ = tab_id;
  return true;
}

bool TabModel::RequestClose(TabId tab_id) {
  TabSnapshot *tab = FindMutable(tab_id);
  if (!tab) {
    return true;
  }
  if (tab->lifecycle == TabLifecycle::kClosing) {
    return true;
  }
  if (tab->lifecycle == TabLifecycle::kCreating) {
    // A creating tab has no bound browser, so no asynchronous close callback
    // will ever arrive; remove it immediately instead of leaking it.
    const std::size_t removed_index = static_cast<std::size_t>(
        std::distance(tabs_.begin(), std::find_if(tabs_.begin(), tabs_.end(),
                                                  [tab_id](const auto &entry) {
                                                    return entry.id == tab_id;
                                                  })));
    const bool removed_active = active_tab_ == tab_id;
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(removed_index));
    if (removed_active) {
      SelectReplacementFor(removed_index);
    }
    return true;
  }
  tab->lifecycle = TabLifecycle::kClosing;
  return true;
}

bool TabModel::DetachBrowser(int browser_id) {
  if (browser_id <= 0) {
    return false;
  }
  const auto found =
      std::find_if(tabs_.begin(), tabs_.end(), [browser_id](const auto &tab) {
        return tab.browser_id == browser_id;
      });
  if (found == tabs_.end()) {
    return false;
  }
  const std::size_t removed_index =
      static_cast<std::size_t>(std::distance(tabs_.begin(), found));
  const bool removed_active = active_tab_ == found->id;
  tabs_.erase(found);
  if (removed_active) {
    SelectReplacementFor(removed_index);
  }
  return true;
}

bool TabModel::MarkCrashed(int browser_id) {
  TabSnapshot *tab = FindByBrowserMutable(browser_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  tab->lifecycle = TabLifecycle::kCrashed;
  tab->loading = false;
  return true;
}

bool TabModel::UpdateAddress(int browser_id, std::string url) {
  TabSnapshot *tab = FindByBrowserMutable(browser_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  tab->url = std::move(url);
  return true;
}

bool TabModel::UpdateLoading(int browser_id, bool loading, bool can_go_back,
                             bool can_go_forward) {
  TabSnapshot *tab = FindByBrowserMutable(browser_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  tab->loading = loading;
  tab->can_go_back = can_go_back;
  tab->can_go_forward = can_go_forward;
  if (tab->lifecycle == TabLifecycle::kCrashed && loading) {
    tab->lifecycle = TabLifecycle::kReady;
  }
  return true;
}

bool TabModel::BeginNavigation(int browser_id) {
  TabSnapshot *tab = FindByBrowserMutable(browser_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing ||
      tab->navigation_generation == UINT64_MAX) {
    return false;
  }
  ++tab->navigation_generation;
  return true;
}

bool TabModel::SetZoom(TabId tab_id, double factor) {
  TabSnapshot *tab = FindMutable(tab_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing ||
      !std::isfinite(factor) || factor < kMinimumWindowZoomFactor ||
      factor > kMaximumWindowZoomFactor) {
    return false;
  }
  tab->zoom_factor = factor;
  return true;
}

const TabSnapshot *TabModel::Find(TabId tab_id) const noexcept {
  const auto found =
      std::find_if(tabs_.begin(), tabs_.end(),
                   [tab_id](const auto &tab) { return tab.id == tab_id; });
  return found == tabs_.end() ? nullptr : &*found;
}

const TabSnapshot *TabModel::FindByBrowser(int browser_id) const noexcept {
  if (browser_id <= 0) {
    // Zero means "no browser bound" on TabSnapshot and is never a valid CEF
    // browser id, so it must not match creating tabs.
    return nullptr;
  }
  const auto found =
      std::find_if(tabs_.begin(), tabs_.end(), [browser_id](const auto &tab) {
        return tab.browser_id == browser_id;
      });
  return found == tabs_.end() ? nullptr : &*found;
}

std::vector<TabId> TabModel::ordered_tabs() const {
  std::vector<TabId> result;
  result.reserve(tabs_.size());
  for (const auto &tab : tabs_) {
    result.push_back(tab.id);
  }
  return result;
}

TabSnapshot *TabModel::FindMutable(TabId tab_id) noexcept {
  return const_cast<TabSnapshot *>(std::as_const(*this).Find(tab_id));
}

TabSnapshot *TabModel::FindByBrowserMutable(int browser_id) noexcept {
  return const_cast<TabSnapshot *>(
      std::as_const(*this).FindByBrowser(browser_id));
}

void TabModel::SelectReplacementFor(std::size_t removed_index) {
  if (tabs_.empty()) {
    active_tab_.reset();
    return;
  }
  const std::size_t replacement_index =
      std::min(removed_index, tabs_.size() - 1);
  active_tab_ = tabs_[replacement_index].id;
}

} // namespace crayon::browser::cef_shell::window
