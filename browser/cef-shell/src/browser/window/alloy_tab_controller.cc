#include "browser/window/alloy_tab_controller.h"

#include <string>
#include <utility>
#include <vector>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

using browser_shell::ContentViewRegistryResult;

bool Accepted(ContentViewRegistryResult result) noexcept {
  return result == ContentViewRegistryResult::kAccepted;
}

void PostClose(CefRefPtr<CefBrowser> browser, bool force_close) {
  CefPostTask(TID_UI, base::BindOnce(
                          [](CefRefPtr<CefBrowser> target, bool force) {
                            target->GetHost()->CloseBrowser(force);
                          },
                          std::move(browser), static_cast<bool>(force_close)));
}

} // namespace

AlloyTabController::AlloyTabController(browser_engine::ProfileId profile_id,
                                       std::size_t capacity)
    : profile_id_(std::move(profile_id)),
      host_(capacity > kMaximumTabsPerWindow ? kMaximumTabsPerWindow
                                             : capacity) {
  CEF_REQUIRE_UI_THREAD();
}

std::optional<TabId>
AlloyTabController::BeginCreate(CefRefPtr<CefBrowserView> view,
                                browser_engine::ContentPurpose purpose,
                                browser_engine::NavigationId navigation_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!view || !view->IsValid() || !browser_engine::IsValid(purpose) ||
      next_mount_epoch_ == 0) {
    return std::nullopt;
  }
  const auto tab_id = model_.CreateTab();
  if (!tab_id.has_value()) {
    return std::nullopt;
  }
  if (!advanced_.AddTab(AdvancedId(*tab_id))) {
    model_.RequestClose(*tab_id);
    return std::nullopt;
  }
  const auto engine_tab_id = EngineTabId(*tab_id);
  const auto epoch = browser_engine::MountEpoch::TryCreate(next_mount_epoch_++);
  if (!engine_tab_id.has_value() || !epoch.has_value()) {
    model_.RequestClose(*tab_id);
    advanced_.OnTabClosed(AdvancedId(*tab_id));
    return std::nullopt;
  }
  browser_engine::ContentViewMountRequest mount{profile_id_, *engine_tab_id,
                                                navigation_id, *epoch, purpose};
  records_.emplace(
      *tab_id,
      Record{mount, view, nullptr,
             browser_engine::ContentCapabilitySet::None(), false, false,
             false, false});
  if (!Accepted(host_.Mount(mount, view))) {
    records_.erase(*tab_id);
    model_.RequestClose(*tab_id);
    advanced_.OnTabClosed(AdvancedId(*tab_id));
    return std::nullopt;
  }
  return tab_id;
}

std::optional<TabId> AlloyTabController::BeginRestore(
    CefRefPtr<CefBrowserView> view,
    browser_engine::NavigationId navigation_id,
    const browser_session::SessionTabSnapshot &snapshot) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser_session::IsValid(snapshot)) {
    return std::nullopt;
  }
  const auto tab_id =
      BeginCreate(view, browser_engine::ContentPurpose::kWeb,
                  navigation_id);
  if (!tab_id) {
    return std::nullopt;
  }
  auto found = records_.find(*tab_id);
  if (found == records_.end() ||
      (snapshot.pinned && !PinTab(*tab_id, true)) ||
      (snapshot.group && !SetTabGroup(*tab_id, snapshot.group))) {
    static_cast<void>(OnCreateFailed(
        view, browser_engine::EngineErrorCode::kCapacityExceeded));
    return std::nullopt;
  }
  found->second.restore_muted = snapshot.muted;
  return tab_id;
}

bool AlloyTabController::OnBrowserCreated(
    CefRefPtr<CefBrowserView> view, CefRefPtr<CefBrowser> browser,
    browser_engine::ContentCapabilitySet capabilities) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || browser->GetIdentifier() <= 0 ||
      browser->GetHost()->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY) {
    return false;
  }
  auto found = FindByView(view);
  if (found == records_.end() || found->second.browser) {
    return false;
  }
  Record &record = found->second;
  record.browser = browser;
  record.capabilities = capabilities;
  if (!Accepted(host_.OnResult(
          {record.mount, browser_engine::ContentViewResultKind::kCreated,
           capabilities, browser_engine::EngineErrorCode::kNone}))) {
    record.browser = nullptr;
    record.capabilities = browser_engine::ContentCapabilitySet::None();
    return false;
  }
  if (record.close_requested || !model_.Find(found->first)) {
    PostClose(browser, true);
    return true;
  }
  if (!model_.BindBrowser(found->first, browser->GetIdentifier())) {
    record.close_requested = true;
    host_.BeginClose(record.mount.tab_id, record.mount.mount_epoch);
    PostClose(browser, true);
    return false;
  }
  if (record.restore_muted && !MuteTab(found->first, true)) {
    record.close_requested = true;
    static_cast<void>(host_.BeginClose(record.mount.tab_id,
                                      record.mount.mount_epoch));
    PostClose(browser, true);
    return false;
  }
  return true;
}

bool AlloyTabController::SynchronizeRuntimeState(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByBrowser(browser);
  if (found == records_.end() || !model_.Find(found->first) ||
      model_.Find(found->first)->lifecycle != TabLifecycle::kReady) {
    return false;
  }
  const bool muted = advanced_.IsMuted(AdvancedId(found->first));
  browser->GetHost()->SetAudioMuted(muted);
  return browser->GetHost()->IsAudioMuted() == muted;
}

bool AlloyTabController::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                         std::string address) {
  CEF_REQUIRE_UI_THREAD();
  return browser && model_.UpdateAddress(browser->GetIdentifier(),
                                         std::move(address));
}

bool AlloyTabController::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                              bool is_loading,
                                              bool can_go_back,
                                              bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) return false;
  const int browser_id = browser->GetIdentifier();
  const TabSnapshot* current = model_.FindByBrowser(browser_id);
  if (!current) return false;
  if (is_loading && !current->loading && !model_.BeginNavigation(browser_id)) {
    return false;
  }
  return model_.UpdateLoading(browser_id, is_loading, can_go_back,
                              can_go_forward);
}

bool AlloyTabController::OnCreateFailed(CefRefPtr<CefBrowserView> view,
                                        browser_engine::EngineErrorCode error) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByView(view);
  if (found == records_.end() || found->second.browser ||
      error == browser_engine::EngineErrorCode::kNone) {
    return false;
  }
  const TabId tab_id = found->first;
  const auto mount = found->second.mount;
  const bool accepted = Accepted(host_.OnResult(
      {mount, browser_engine::ContentViewResultKind::kCreateFailed,
       browser_engine::ContentCapabilitySet::None(), error}));
  if (!accepted || !model_.RequestClose(tab_id)) {
    return false;
  }
  advanced_.OnTabClosed(AdvancedId(tab_id));
  records_.erase(found);
  return true;
}

bool AlloyTabController::Activate(TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(tab_id);
  const auto *tab = model_.Find(tab_id);
  if (found == records_.end() || !tab ||
      tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  return Accepted(host_.Activate(found->second.mount.tab_id,
                                 found->second.mount.mount_epoch)) &&
         model_.Activate(tab_id) && advanced_.ActivateTab(AdvancedId(tab_id));
}

bool AlloyTabController::PinTab(TabId tab_id, bool pinned) {
  CEF_REQUIRE_UI_THREAD();
  const auto *tab = model_.Find(tab_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  return pinned ? advanced_.PinTab(AdvancedId(tab_id))
                : advanced_.UnpinTab(AdvancedId(tab_id));
}

bool AlloyTabController::MuteTab(TabId tab_id, bool muted) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(tab_id);
  const auto *tab = model_.Find(tab_id);
  if (found == records_.end() || !tab ||
      tab->lifecycle != TabLifecycle::kReady || !found->second.browser) {
    return false;
  }
  const std::string id = AdvancedId(tab_id);
  const bool changed = muted ? advanced_.MuteTab(id) : advanced_.UnmuteTab(id);
  if (!changed) {
    return false;
  }
  found->second.browser->GetHost()->SetAudioMuted(muted);
  return true;
}

bool AlloyTabController::SetTabGroup(TabId tab_id,
                                     std::optional<std::string> group) {
  CEF_REQUIRE_UI_THREAD();
  const auto *tab = model_.Find(tab_id);
  if (!tab || tab->lifecycle == TabLifecycle::kClosing) {
    return false;
  }
  return group ? advanced_.AddTabToGroup(AdvancedId(tab_id), *group)
               : advanced_.RemoveTabFromGroup(AdvancedId(tab_id));
}

bool AlloyTabController::CopyAdvancedState(TabId source_tab_id,
                                           TabId target_tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!advanced_.CopyTabState(AdvancedId(source_tab_id),
                              AdvancedId(target_tab_id))) {
    return false;
  }
  auto target = records_.find(target_tab_id);
  if (target != records_.end() && target->second.browser &&
      advanced_.IsMuted(AdvancedId(target_tab_id))) {
    target->second.browser->GetHost()->SetAudioMuted(true);
  }
  return true;
}

std::vector<TabId>
AlloyTabController::SearchTabs(const std::string &query) const {
  const auto matches = advanced_.SearchTabs(query);
  std::vector<TabId> result;
  for (TabId id : model_.ordered_tabs()) {
    if (std::find(matches.begin(), matches.end(), AdvancedId(id)) !=
        matches.end()) {
      result.push_back(id);
    }
  }
  return result;
}

std::vector<TabId> AlloyTabController::advanced_ordered_tabs() const {
  const auto order = advanced_.ordered_tabs();
  std::vector<TabId> result;
  result.reserve(order.size());
  for (const auto &advanced_id : order) {
    for (TabId id : model_.ordered_tabs()) {
      if (AdvancedId(id) == advanced_id) {
        result.push_back(id);
        break;
      }
    }
  }
  return result;
}

bool AlloyTabController::IsPinned(TabId tab_id) const {
  return model_.Find(tab_id) && advanced_.IsPinned(AdvancedId(tab_id));
}

bool AlloyTabController::IsMuted(TabId tab_id) const {
  return model_.Find(tab_id) && advanced_.IsMuted(AdvancedId(tab_id));
}

std::optional<std::string> AlloyTabController::TabGroup(TabId tab_id) const {
  return model_.Find(tab_id) ? advanced_.GetTabGroup(AdvancedId(tab_id))
                             : std::nullopt;
}

std::optional<std::vector<browser_session::SessionTabSnapshot>>
AlloyTabController::SnapshotSessionTabs() const {
  CEF_REQUIRE_UI_THREAD();
  std::vector<browser_session::SessionTabSnapshot> tabs;
  for (TabId id : model_.ordered_tabs()) {
    const auto record = records_.find(id);
    const auto *snapshot = model_.Find(id);
    if (record == records_.end() || !snapshot ||
        snapshot->lifecycle != TabLifecycle::kReady ||
        !record->second.browser) {
      return std::nullopt;
    }
    std::string url = snapshot->url;
    if (url.empty() && record->second.browser->GetMainFrame()) {
      url = record->second.browser->GetMainFrame()->GetURL();
    }
    browser_session::SessionTabSnapshot tab{
        std::move(url), advanced_.IsPinned(AdvancedId(id)),
        advanced_.IsMuted(AdvancedId(id)),
        advanced_.GetTabGroup(AdvancedId(id))};
    if (!browser_session::IsValid(tab)) {
      return std::nullopt;
    }
    tabs.push_back(std::move(tab));
  }
  return tabs.empty()
             ? std::nullopt
             : std::optional<std::vector<browser_session::SessionTabSnapshot>>(
                   std::move(tabs));
}

std::optional<std::size_t> AlloyTabController::active_tab_index() const {
  const auto active = model_.active_tab();
  if (!active) {
    return std::nullopt;
  }
  const auto order = model_.ordered_tabs();
  const auto found = std::find(order.begin(), order.end(), *active);
  return found == order.end()
             ? std::nullopt
             : std::optional<std::size_t>(
                   static_cast<std::size_t>(found - order.begin()));
}

bool AlloyTabController::BeginClose(TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(tab_id);
  if (found == records_.end()) {
    return false;
  }
  Record &record = found->second;
  if (!record.close_requested) {
    if (!Accepted(
            host_.BeginClose(record.mount.tab_id, record.mount.mount_epoch)) ||
        !model_.RequestClose(tab_id)) {
      return false;
    }
    record.close_requested = true;
  }
  return true;
}

bool AlloyTabController::RequestClose(TabId tab_id, bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  if (!BeginClose(tab_id)) {
    return false;
  }
  auto found = records_.find(tab_id);
  if (found == records_.end()) {
    return false;
  }
  Record &record = found->second;
  if (record.browser) {
    if (force_close) {
      record.browser->GetHost()->CloseBrowser(true);
    } else {
      record.browser->GetHost()->TryCloseBrowser();
    }
  }
  return true;
}

bool AlloyTabController::CancelClose(TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(tab_id);
  if (found == records_.end() || !found->second.close_requested ||
      !found->second.browser || found->second.view_released) {
    return false;
  }
  Record &record = found->second;
  if (!Accepted(host_.OnResult(
          {record.mount, browser_engine::ContentViewResultKind::kCloseCancelled,
           browser_engine::ContentCapabilitySet::None(),
           browser_engine::EngineErrorCode::kNone})) ||
      !model_.CancelClose(tab_id)) {
    return false;
  }
  record.close_requested = false;
  return Activate(tab_id);
}

bool AlloyTabController::OnDoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByBrowser(browser);
  if (found == records_.end()) {
    return false;
  }
  Record &record = found->second;
  if (!record.close_requested) {
    if (!Accepted(
            host_.BeginClose(record.mount.tab_id, record.mount.mount_epoch))) {
      return false;
    }
    model_.RequestClose(found->first);
    record.close_requested = true;
  }
  return true;
}

bool AlloyTabController::ReleaseAfterDoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByBrowser(browser);
  if (found == records_.end() || !found->second.close_requested) {
    return false;
  }
  Record &record = found->second;
  if (!record.view_released &&
      !Accepted(host_.ReleaseViewForClose(record.mount.tab_id,
                                          record.mount.mount_epoch))) {
    return false;
  }
  record.view_released = true;
  record.view = nullptr;
  return true;
}

bool AlloyTabController::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByBrowser(browser);
  if (found == records_.end()) {
    return false;
  }
  const TabId closing_tab = found->first;
  const bool closing_active = model_.active_tab() == closing_tab;
  Record &record = found->second;
  if (!record.close_requested) {
    if (!Accepted(
            host_.BeginClose(record.mount.tab_id, record.mount.mount_epoch))) {
      return false;
    }
    model_.RequestClose(found->first);
  }
  const bool closed =
      record.terminal_reported ||
      Accepted(host_.OnResult({record.mount,
                               browser_engine::ContentViewResultKind::kClosed,
                               browser_engine::ContentCapabilitySet::None(),
                               browser_engine::EngineErrorCode::kNone}));
  if (const auto *tab = model_.Find(found->first); tab && tab->browser_id > 0) {
    model_.DetachBrowser(tab->browser_id);
  }
  advanced_.OnTabClosed(AdvancedId(found->first));
  records_.erase(found);
  if (closing_active) {
    const auto replacement = model_.active_tab();
    if (replacement) {
      const auto successor = records_.find(*replacement);
      if (successor == records_.end() || successor->second.close_requested ||
          successor->second.view_released ||
          !Accepted(host_.Activate(successor->second.mount.tab_id,
                                   successor->second.mount.mount_epoch))) {
        return false;
      }
    }
  }
  return closed;
}

bool AlloyTabController::OnRenderProcessGone(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByBrowser(browser);
  if (found == records_.end() ||
      !model_.MarkCrashed(browser->GetIdentifier())) {
    return false;
  }
  found->second.close_requested = true;
  return true;
}

bool AlloyTabController::FinalizeRendererCrash(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  auto found = FindByBrowser(browser);
  if (found == records_.end() || !found->second.close_requested) {
    return false;
  }
  Record &record = found->second;
  const bool crashed = Accepted(host_.OnResult(
      {record.mount, browser_engine::ContentViewResultKind::kCrashed,
       browser_engine::ContentCapabilitySet::None(),
       browser_engine::EngineErrorCode::kNavigationFailed}));
  record.view_released = true;
  record.view = nullptr;
  record.close_requested = true;
  record.terminal_reported = crashed;
  PostClose(browser, true);
  return crashed;
}

std::optional<AlloyTabController::TransferredTab>
AlloyTabController::TransferOut(TabId tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(tab_id);
  const auto *snapshot = model_.Find(tab_id);
  if (found == records_.end() || !snapshot ||
      snapshot->lifecycle != TabLifecycle::kReady ||
      found->second.close_requested || found->second.view_released ||
      !found->second.view || !found->second.browser ||
      !Accepted(host_.DetachForTransfer(found->second.mount.tab_id,
                                        found->second.mount.mount_epoch))) {
    return std::nullopt;
  }
  TransferredTab transfer{tab_id,
                          found->second.mount,
                          found->second.capabilities,
                          *snapshot,
                          std::move(found->second.view),
                          std::move(found->second.browser),
                          advanced_.IsPinned(AdvancedId(tab_id)),
                          advanced_.IsMuted(AdvancedId(tab_id)),
                          advanced_.GetTabGroup(AdvancedId(tab_id))};
  records_.erase(found);
  if (!model_.DetachBrowser(transfer.snapshot.browser_id)) {
    return std::nullopt;
  }
  advanced_.OnTabClosed(AdvancedId(tab_id));
  return transfer;
}

std::optional<TabId>
AlloyTabController::AdoptTransfer(TransferredTab transfer, bool preserve_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!transfer.view || !transfer.browser || next_mount_epoch_ == 0 ||
      transfer.browser->GetIdentifier() != transfer.snapshot.browser_id ||
      transfer.snapshot.lifecycle != TabLifecycle::kReady) {
    return std::nullopt;
  }
  const auto adopted = model_.AdoptTransferred(transfer.snapshot, preserve_id);
  if (!adopted) {
    return std::nullopt;
  }
  if (!advanced_.AddTab(AdvancedId(*adopted))) {
    model_.DetachBrowser(transfer.snapshot.browser_id);
    return std::nullopt;
  }
  const auto engine_tab_id = EngineTabId(*adopted);
  const auto epoch = browser_engine::MountEpoch::TryCreate(next_mount_epoch_++);
  if (!engine_tab_id || !epoch) {
    model_.DetachBrowser(transfer.snapshot.browser_id);
    advanced_.OnTabClosed(AdvancedId(*adopted));
    return std::nullopt;
  }
  browser_engine::ContentViewMountRequest mount{
      profile_id_, *engine_tab_id, transfer.previous_mount.navigation_id,
      *epoch, transfer.previous_mount.purpose};
  if (!Accepted(host_.Mount(mount, transfer.view)) ||
      !Accepted(host_.OnResult(
          {mount, browser_engine::ContentViewResultKind::kCreated,
           transfer.capabilities, browser_engine::EngineErrorCode::kNone}))) {
    model_.DetachBrowser(transfer.snapshot.browser_id);
    advanced_.OnTabClosed(AdvancedId(*adopted));
    return std::nullopt;
  }
  if (!Accepted(host_.Activate(mount.tab_id, mount.mount_epoch))) {
    static_cast<void>(
        host_.DetachForTransfer(mount.tab_id, mount.mount_epoch));
    model_.DetachBrowser(transfer.snapshot.browser_id);
    advanced_.OnTabClosed(AdvancedId(*adopted));
    return std::nullopt;
  }
  records_.emplace(*adopted,
                   Record{mount, std::move(transfer.view),
                          std::move(transfer.browser), transfer.capabilities,
                          false, false, false, false});
  if (transfer.pinned) {
    advanced_.PinTab(AdvancedId(*adopted));
  }
  if (transfer.muted) {
    advanced_.MuteTab(AdvancedId(*adopted));
    records_.at(*adopted).browser->GetHost()->SetAudioMuted(true);
  }
  if (transfer.group) {
    advanced_.AddTabToGroup(AdvancedId(*adopted), *transfer.group);
  }
  return adopted;
}

void AlloyTabController::CloseAll(bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  if (!force_close) {
    static_cast<void>(RequestNextClose(false));
    return;
  }
  std::vector<TabId> ids;
  ids.reserve(records_.size());
  for (const auto &entry : records_) {
    ids.push_back(entry.first);
  }
  for (TabId id : ids) {
    RequestClose(id, force_close);
  }
}

bool AlloyTabController::RequestNextClose(bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  const auto active = model_.active_tab();
  if (active) {
    const auto found = records_.find(*active);
    if (found != records_.end() && !found->second.close_requested) {
      return RequestClose(*active, force_close);
    }
  }
  for (TabId id : model_.ordered_tabs()) {
    const auto found = records_.find(id);
    if (found != records_.end() && !found->second.close_requested) {
      return RequestClose(id, force_close);
    }
  }
  return false;
}

bool AlloyTabController::ReleaseAfterClosed() {
  CEF_REQUIRE_UI_THREAD();
  if (!records_.empty() || !model_.empty() || !host_.ReleaseAfterClosed()) {
    return false;
  }
  advanced_.Shutdown();
  return true;
}

bool AlloyTabController::OwnsBrowser(CefRefPtr<CefBrowser> browser) const {
  CEF_REQUIRE_UI_THREAD();
  return FindByBrowser(std::move(browser)) != records_.end();
}

bool AlloyTabController::OwnsView(CefRefPtr<CefBrowserView> view) const {
  CEF_REQUIRE_UI_THREAD();
  return FindByView(std::move(view)) != records_.end();
}

AlloyTabController::RecordMap::iterator
AlloyTabController::FindByView(CefRefPtr<CefBrowserView> view) {
  if (!view) {
    return records_.end();
  }
  for (auto found = records_.begin(); found != records_.end(); ++found) {
    if (found->second.view && found->second.view->IsSame(view)) {
      return found;
    }
  }
  return records_.end();
}

AlloyTabController::RecordMap::const_iterator
AlloyTabController::FindByView(CefRefPtr<CefBrowserView> view) const {
  if (!view) {
    return records_.end();
  }
  for (auto found = records_.begin(); found != records_.end(); ++found) {
    if (found->second.view && found->second.view->IsSame(view)) {
      return found;
    }
  }
  return records_.end();
}

AlloyTabController::RecordMap::iterator
AlloyTabController::FindByBrowser(CefRefPtr<CefBrowser> browser) {
  if (!browser) {
    return records_.end();
  }
  for (auto found = records_.begin(); found != records_.end(); ++found) {
    if (found->second.browser && found->second.browser->IsSame(browser)) {
      return found;
    }
  }
  return records_.end();
}

AlloyTabController::RecordMap::const_iterator
AlloyTabController::FindByBrowser(CefRefPtr<CefBrowser> browser) const {
  if (!browser) {
    return records_.end();
  }
  for (auto found = records_.begin(); found != records_.end(); ++found) {
    if (found->second.browser && found->second.browser->IsSame(browser)) {
      return found;
    }
  }
  return records_.end();
}

std::optional<browser_engine::TabId>
AlloyTabController::EngineTabId(TabId tab_id) {
  return browser_engine::TabId::TryCreate("tab-" + std::to_string(tab_id));
}

std::string AlloyTabController::AdvancedId(TabId tab_id) {
  return "tab-" + std::to_string(tab_id);
}

} // namespace crayon::browser::cef_shell::window
