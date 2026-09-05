#include "browser/window/alloy_content_view_host.h"

#include <cmath>
#include <utility>

#include "include/views/cef_fill_layout.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

using browser_shell::ContentViewLifecycleState;
using browser_shell::ContentViewRegistryResult;

bool IsTerminal(browser_engine::ContentViewResultKind kind) noexcept {
  return kind == browser_engine::ContentViewResultKind::kCreateFailed ||
         kind == browser_engine::ContentViewResultKind::kClosed ||
         kind == browser_engine::ContentViewResultKind::kCrashed;
}

} // namespace

AlloyContentViewHost::AlloyContentViewHost(std::size_t capacity)
    : registry_(capacity), container_(CefPanel::CreatePanel(nullptr)) {
  CEF_REQUIRE_UI_THREAD();
  if (container_) {
    container_->SetToFillLayout();
  }
}

ContentViewRegistryResult AlloyContentViewHost::Mount(
    const browser_engine::ContentViewMountRequest &request,
    CefRefPtr<CefBrowserView> view) {
  CEF_REQUIRE_UI_THREAD();
  if (!container_ || !view || !view->IsValid()) {
    return ContentViewRegistryResult::kInvalidArgument;
  }
  const auto result = registry_.BeginMount(request);
  if (result != ContentViewRegistryResult::kAccepted) {
    return result;
  }
  view->SetVisible(false);
  container_->AddChildView(view);
  views_.emplace(request.tab_id.value(),
                 HostedView{request.mount_epoch, std::move(view)});
  return ContentViewRegistryResult::kAccepted;
}

ContentViewRegistryResult AlloyContentViewHost::OnResult(
    const browser_engine::ContentViewResult &result) {
  CEF_REQUIRE_UI_THREAD();
  const auto status = registry_.OnResult(result);
  if (status == ContentViewRegistryResult::kAccepted &&
      IsTerminal(result.kind)) {
    RemoveView(result.mount.tab_id);
    return registry_.ForgetDetachedTab(result.mount.tab_id);
  }
  return status;
}

ContentViewRegistryResult
AlloyContentViewHost::Activate(const browser_engine::TabId &tab_id,
                               browser_engine::MountEpoch epoch) {
  CEF_REQUIRE_UI_THREAD();
  const auto *record = FindCurrent(tab_id, epoch);
  if (!record) {
    const auto *existing = registry_.Find(tab_id);
    return existing ? ContentViewRegistryResult::kStaleEpoch
                    : ContentViewRegistryResult::kNotFound;
  }
  if (record->state != ContentViewLifecycleState::kMounted) {
    return ContentViewRegistryResult::kInvalidState;
  }
  const auto selected = views_.find(tab_id.value());
  if (selected == views_.end() || !selected->second.view) {
    return ContentViewRegistryResult::kInvalidState;
  }
  for (auto &[id, hosted] : views_) {
    hosted.view->SetVisible(id == tab_id.value());
  }
  container_->Layout();
  selected->second.view->RequestFocus();
  active_tab_id_ = tab_id.value();
  return ContentViewRegistryResult::kAccepted;
}

ContentViewRegistryResult
AlloyContentViewHost::SetZoom(const browser_engine::TabId &tab_id,
                              browser_engine::MountEpoch epoch,
                              browser_engine::ZoomFactor factor) {
  CEF_REQUIRE_UI_THREAD();
  const auto capability = registry_.RequireCapability(
      tab_id, epoch, browser_engine::ContentCapability::kZoom);
  if (capability != ContentViewRegistryResult::kAccepted) {
    return capability;
  }
  const auto hosted = views_.find(tab_id.value());
  if (hosted == views_.end() || !hosted->second.view ||
      !hosted->second.view->GetBrowser()) {
    return ContentViewRegistryResult::kInvalidState;
  }
  constexpr double kChromiumZoomRatio = 1.2;
  const double level = std::log(factor.value()) / std::log(kChromiumZoomRatio);
  hosted->second.view->GetBrowser()->GetHost()->SetZoomLevel(level);
  return ContentViewRegistryResult::kAccepted;
}

ContentViewRegistryResult
AlloyContentViewHost::BeginClose(const browser_engine::TabId &tab_id,
                                 browser_engine::MountEpoch epoch) {
  CEF_REQUIRE_UI_THREAD();
  return registry_.BeginClose(tab_id, epoch);
}

ContentViewRegistryResult
AlloyContentViewHost::ReleaseViewForClose(const browser_engine::TabId &tab_id,
                                          browser_engine::MountEpoch epoch) {
  CEF_REQUIRE_UI_THREAD();
  const auto *record = FindCurrent(tab_id, epoch);
  if (!record) {
    return registry_.Find(tab_id) ? ContentViewRegistryResult::kStaleEpoch
                                  : ContentViewRegistryResult::kNotFound;
  }
  if (record->state != ContentViewLifecycleState::kClosing) {
    return ContentViewRegistryResult::kInvalidState;
  }
  RemoveView(tab_id);
  return ContentViewRegistryResult::kAccepted;
}

ContentViewRegistryResult AlloyContentViewHost::DetachForTransfer(
    const browser_engine::TabId &tab_id, browser_engine::MountEpoch epoch) {
  CEF_REQUIRE_UI_THREAD();
  const auto *record = FindCurrent(tab_id, epoch);
  const auto hosted = views_.find(tab_id.value());
  if (!record || record->state != ContentViewLifecycleState::kMounted ||
      hosted == views_.end() || !hosted->second.view) {
    return ContentViewRegistryResult::kInvalidState;
  }
  const auto mount = record->mount;
  if (registry_.OnResult(
          {mount, browser_engine::ContentViewResultKind::kClosed,
           browser_engine::ContentCapabilitySet::None(),
           browser_engine::EngineErrorCode::kNone}) !=
      ContentViewRegistryResult::kAccepted) {
    return ContentViewRegistryResult::kInvalidState;
  }
  if (container_) {
    container_->RemoveChildView(hosted->second.view);
  }
  views_.erase(hosted);
  if (active_tab_id_ == tab_id.value()) {
    active_tab_id_.reset();
  }
  return registry_.ForgetDetachedTab(tab_id);
}

bool AlloyContentViewHost::ReleaseAfterClosed() {
  CEF_REQUIRE_UI_THREAD();
  if (!views_.empty()) {
    return false;
  }
  if (container_) {
    container_->RemoveAllChildViews();
    container_ = nullptr;
  }
  active_tab_id_.reset();
  registry_.Shutdown();
  return true;
}

const browser_shell::ContentViewRecord *AlloyContentViewHost::FindCurrent(
    const browser_engine::TabId &tab_id,
    browser_engine::MountEpoch epoch) const noexcept {
  const auto *record = registry_.Find(tab_id);
  return record && record->mount.mount_epoch == epoch ? record : nullptr;
}

void AlloyContentViewHost::RemoveView(const browser_engine::TabId &tab_id) {
  const auto hosted = views_.find(tab_id.value());
  if (hosted == views_.end()) {
    return;
  }
  if (container_ && hosted->second.view) {
    container_->RemoveChildView(hosted->second.view);
  }
  views_.erase(hosted);
  if (active_tab_id_ == tab_id.value()) {
    active_tab_id_.reset();
  }
}

} // namespace crayon::browser::cef_shell::window
