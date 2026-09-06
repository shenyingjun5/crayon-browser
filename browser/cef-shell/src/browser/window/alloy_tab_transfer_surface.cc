#include "browser/window/alloy_tab_transfer_surface.h"

#include <set>
#include <utility>

#include "crayon/browser_localization/locale_catalog.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {

AlloyTabTransferSurface::AlloyTabTransferSurface(
    localization::LocaleSnapshot locale, Callbacks callbacks)
    : locale_(std::move(locale)), callbacks_(std::move(callbacks)) {
  CEF_REQUIRE_UI_THREAD();
}

bool AlloyTabTransferSurface::Attach(CefRefPtr<CefWindow> window,
                                     CefRefPtr<CefPanel> toolbar) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || window_ || !window || !toolbar || !window->IsValid() ||
      !toolbar->IsValid() || !toolbar->GetWindow() ||
      !toolbar->GetWindow()->IsSame(window)) {
    return false;
  }
  const auto label = String("tabs.move_to_window");
  if (label.empty()) return false;
  button_ = CefMenuButton::CreateMenuButton(this, label);
  if (!button_) return false;
  window_ = std::move(window);
  toolbar_ = std::move(toolbar);
  button_->SetID(kButtonId);
  button_->SetAccessibleName(label);
  button_->SetTooltipText(label);
  toolbar_->AddChildView(button_);
  return Refresh();
}

bool AlloyTabTransferSurface::Refresh() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !button_ || !button_->IsValid() || menu_model_) {
    return false;
  }
  const auto targets = callbacks_.targets
                           ? callbacks_.targets()
                           : std::vector<AlloyTabTransferTarget>{};
  button_->SetEnabled(!targets.empty());
  if (toolbar_) toolbar_->Layout();
  return true;
}

CefRefPtr<CefView> AlloyTabTransferSurface::button() const {
  CEF_REQUIRE_UI_THREAD();
  return active_ ? button_ : nullptr;
}

void AlloyTabTransferSurface::OnButtonPressed(CefRefPtr<CefButton>) {}

void AlloyTabTransferSurface::OnMenuButtonPressed(
    CefRefPtr<CefMenuButton> menu_button, const CefPoint& screen_point,
    CefRefPtr<CefMenuButtonPressedLock>) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || menu_model_ || !menu_button || !button_ ||
      !menu_button->IsSame(button_) || !callbacks_.targets) {
    return;
  }
  const auto candidates = callbacks_.targets();
  auto model = CefMenuModel::CreateMenuModel(this);
  if (!model) return;
  std::set<std::string> unique;
  target_ids_.clear();
  for (const auto& candidate : candidates) {
    if (target_ids_.size() >= kMaximumTargets) break;
    if (candidate.window_id.empty() || candidate.label.empty() ||
        !unique.insert(candidate.window_id).second) {
      continue;
    }
    model->AddItem(kTargetCommandBase + static_cast<int>(target_ids_.size()),
                   candidate.label);
    target_ids_.push_back(candidate.window_id);
  }
  if (target_ids_.empty()) return;
  menu_model_ = model;
  menu_button->ShowMenu(menu_model_, screen_point, CEF_MENU_ANCHOR_TOPRIGHT);
}

void AlloyTabTransferSurface::ExecuteCommand(CefRefPtr<CefMenuModel>,
                                             int command_id,
                                             cef_event_flags_t) {
  CEF_REQUIRE_UI_THREAD();
  const int index = command_id - kTargetCommandBase;
  if (!active_ || index < 0 ||
      static_cast<std::size_t>(index) >= target_ids_.size() ||
      !callbacks_.move_to) {
    return;
  }
  const std::string target = target_ids_[static_cast<std::size_t>(index)];
  auto move_to = callbacks_.move_to;
  // Moving may synchronously relayout or destroy the source window. Release
  // the transient menu state before entering product callbacks so surviving
  // windows can refresh and reopen this surface after the command closes.
  menu_model_ = nullptr;
  target_ids_.clear();
  static_cast<void>(move_to(target));
  static_cast<void>(Refresh());
}

void AlloyTabTransferSurface::MenuClosed(CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  if (!menu_model_ || !model || menu_model_.get() != model.get()) return;
  menu_model_ = nullptr;
  target_ids_.clear();
  static_cast<void>(Refresh());
}

bool AlloyTabTransferSurface::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) return true;
  active_ = false;
  menu_model_ = nullptr;
  target_ids_.clear();
  if (toolbar_ && toolbar_->IsValid() && button_ && button_->IsValid() &&
      button_->GetParentView() && button_->GetParentView()->IsSame(toolbar_)) {
    toolbar_->RemoveChildView(button_);
  }
  button_ = nullptr;
  toolbar_ = nullptr;
  window_ = nullptr;
  callbacks_ = {};
  return true;
}

std::string AlloyTabTransferSurface::String(const char* key) const {
  const localization::LocaleCatalog catalog(locale_.locale);
  const auto value = catalog.Find(key ? key : "");
  return value ? std::string(*value) : std::string{};
}

}  // namespace crayon::browser::cef_shell::window
