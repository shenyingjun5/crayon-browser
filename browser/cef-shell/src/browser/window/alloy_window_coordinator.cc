#include "browser/window/alloy_window_coordinator.h"

#include <limits>
#include <utility>

#include "browser/window/popup_target.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {

AlloyWindowCoordinator::AlloyWindowCoordinator(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
  CEF_REQUIRE_UI_THREAD();
}

AlloyWindowCoordinator::~AlloyWindowCoordinator() {
  if (active_) {
    Shutdown();
  }
}

bool AlloyWindowCoordinator::CreatePrimary(
    std::string window_id, browser_engine::ProfileId profile_id,
    bool restorable) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || dispatching_ || !windows_.CreateWindow(window_id)) {
    return false;
  }
  records_.emplace(window_id, Record{profile_id,
                                     std::make_unique<AlloyTabController>(
                                         std::move(profile_id)),
                                     nullptr, false, restorable});
  return true;
}

std::optional<AlloyWindowCoordinator::PopupRequest>
AlloyWindowCoordinator::RequestPopup(const std::string &opener_window_id,
                                     CefRefPtr<CefBrowser> source_browser,
                                     std::string target_url,
                                     bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  auto opener = records_.find(opener_window_id);
  if (!active_ || dispatching_ || opener == records_.end() ||
      opener->second.closing ||
      !opener->second.controller->OwnsBrowser(source_browser) ||
      !IsPopupUrlAllowed(target_url)) {
    return std::nullopt;
  }
  const std::string popup_id = NextPopupId();
  if (popup_id.empty() ||
      !browser_windows::IsAllowed(windows_.RequestPopup(
          opener_window_id, popup_id,
          user_gesture ? browser_windows::PopupSource::kUserGesture
                       : browser_windows::PopupSource::kProgrammatic))) {
    return std::nullopt;
  }
  PopupRequest request{popup_id, opener_window_id, std::move(target_url)};
  records_.emplace(popup_id, Record{opener->second.profile_id,
                                    std::make_unique<AlloyTabController>(
                                        opener->second.profile_id),
                                    nullptr, false, opener->second.restorable});
  dispatching_ = true;
  const bool created =
      callbacks_.create_popup && callbacks_.create_popup(request);
  dispatching_ = false;
  if (!created) {
    records_.erase(popup_id);
    windows_.CloseWindow(popup_id);
    return std::nullopt;
  }
  return request;
}

bool AlloyWindowCoordinator::AttachWindow(const std::string &window_id,
                                          CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(window_id);
  if (!active_ || found == records_.end() || found->second.closing ||
      found->second.window || !window) {
    return false;
  }
  found->second.window = std::move(window);
  return true;
}

bool AlloyWindowCoordinator::CancelPendingWindow(
    const std::string &window_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(window_id);
  if (!active_ || dispatching_ || found == records_.end() ||
      found->second.window || found->second.controller->model().size() != 0) {
    return false;
  }
  records_.erase(found);
  return windows_.CloseWindow(window_id);
}

bool AlloyWindowCoordinator::RestoreSession(
    const browser_session::SessionProfileSnapshot &snapshot,
    const browser_engine::ProfileId &profile_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || dispatching_ || !records_.empty() ||
      !callbacks_.restore_window || snapshot.profile_id != profile_id.value() ||
      !browser_session::IsValid(snapshot) ||
      snapshot.windows.size() > browser_windows::kMaxWindows) {
    return false;
  }
  for (const auto &window : snapshot.windows) {
    if (window.window_id.size() > browser_windows::kMaxWindowIdLength) {
      return false;
    }
  }
  for (const auto &window : snapshot.windows) {
    if (!windows_.CreateWindow(window.window_id)) {
      return false;
    }
    records_.emplace(
        window.window_id,
        Record{profile_id, std::make_unique<AlloyTabController>(profile_id),
               nullptr, false, true});
  }
  dispatching_ = true;
  for (const auto &window : snapshot.windows) {
    if (!callbacks_.restore_window(window)) {
      dispatching_ = false;
      for (auto &[id, record] : records_) {
        static_cast<void>(id);
        record.closing = true;
        record.controller->CloseAll(true);
      }
      return false;
    }
  }
  dispatching_ = false;
  return true;
}

bool AlloyWindowCoordinator::FocusWindow(const std::string &window_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(window_id);
  if (!active_ || dispatching_ || found == records_.end() ||
      found->second.closing || !windows_.FocusWindow(window_id)) {
    return false;
  }
  if (found->second.window) {
    found->second.window->Activate();
  }
  if (callbacks_.focus_window) {
    callbacks_.focus_window(window_id);
  }
  return true;
}

std::optional<TabId>
AlloyWindowCoordinator::MoveTab(const std::string &source_window_id,
                                TabId source_tab_id,
                                const std::string &target_window_id) {
  CEF_REQUIRE_UI_THREAD();
  auto source = records_.find(source_window_id);
  auto target = records_.find(target_window_id);
  if (!active_ || dispatching_ || source_window_id == target_window_id ||
      source == records_.end() || target == records_.end() ||
      source->second.closing || target->second.closing ||
      source->second.profile_id != target->second.profile_id ||
      target->second.controller->model().size() >= kMaximumTabsPerWindow) {
    return std::nullopt;
  }
  auto transfer = source->second.controller->TransferOut(source_tab_id);
  if (!transfer) {
    return std::nullopt;
  }
  const auto adopted = target->second.controller->AdoptTransfer(*transfer);
  if (!adopted) {
    static_cast<void>(
        source->second.controller->AdoptTransfer(std::move(*transfer), true));
    return std::nullopt;
  }
  return adopted;
}

bool AlloyWindowCoordinator::BeginCloseWindow(const std::string &window_id,
                                              bool force_close) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(window_id);
  if (!active_ || dispatching_ || found == records_.end() ||
      found->second.closing) {
    return false;
  }
  found->second.closing = true;
  found->second.controller->CloseAll(force_close);
  return true;
}

bool AlloyWindowCoordinator::OnWindowClosed(const std::string &window_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = records_.find(window_id);
  if (!active_ || dispatching_ || found == records_.end() ||
      !found->second.closing ||
      !found->second.controller->ReleaseAfterClosed()) {
    return false;
  }
  found->second.window = nullptr;
  records_.erase(found);
  if (!windows_.CloseWindow(window_id)) {
    return false;
  }
  const auto focused = windows_.focused_window_id();
  if (focused && callbacks_.focus_window) {
    callbacks_.focus_window(*focused);
  }
  return true;
}

bool AlloyWindowCoordinator::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) {
    return true;
  }
  if (dispatching_) {
    return false;
  }
  for (auto &[id, record] : records_) {
    static_cast<void>(id);
    record.closing = true;
    record.controller->CloseAll(true);
  }
  if (!records_.empty()) {
    return false;
  }
  active_ = false;
  callbacks_ = {};
  windows_.Shutdown();
  return true;
}

AlloyTabController *
AlloyWindowCoordinator::controller(const std::string &window_id) noexcept {
  auto found = records_.find(window_id);
  return active_ && found != records_.end() ? found->second.controller.get()
                                            : nullptr;
}

bool AlloyWindowCoordinator::has_window(
    const std::string &window_id) const noexcept {
  return active_ && records_.count(window_id) != 0;
}

bool AlloyWindowCoordinator::is_popup(const std::string &window_id) const {
  return windows_.IsPopup(window_id);
}

std::optional<std::string>
AlloyWindowCoordinator::opener_of(const std::string &window_id) const {
  return windows_.OpenerOf(window_id);
}

std::optional<std::string> AlloyWindowCoordinator::focused_window_id() const {
  return windows_.focused_window_id();
}

std::size_t AlloyWindowCoordinator::window_count() const noexcept {
  return records_.size();
}

std::optional<browser_session::SessionProfileSnapshot>
AlloyWindowCoordinator::SnapshotSession(
    const browser_engine::ProfileId &profile_id) const {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || dispatching_) {
    return std::nullopt;
  }
  browser_session::SessionProfileSnapshot snapshot{profile_id.value(), {}};
  for (const auto &[id, record] : records_) {
    if (!record.restorable || record.closing || record.profile_id != profile_id) {
      continue;
    }
    const auto tabs = record.controller->SnapshotSessionTabs();
    const auto active_index = record.controller->active_tab_index();
    if (!tabs || !active_index) {
      return std::nullopt;
    }
    snapshot.windows.push_back({id, *tabs, *active_index});
  }
  return browser_session::IsValid(snapshot)
             ? std::optional<browser_session::SessionProfileSnapshot>(
                   std::move(snapshot))
             : std::nullopt;
}

std::string AlloyWindowCoordinator::NextPopupId() {
  if (next_popup_id_ == 0 ||
      next_popup_id_ == std::numeric_limits<std::uint64_t>::max()) {
    return {};
  }
  return "popup-" + std::to_string(next_popup_id_++);
}

} // namespace crayon::browser::cef_shell::window
