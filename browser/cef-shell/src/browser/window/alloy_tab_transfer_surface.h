#pragma once

#include <functional>
#include <string>
#include <vector>

#include "crayon/browser_localization/locale_snapshot.h"
#include "include/cef_menu_model_delegate.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_menu_button_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"

namespace crayon::browser::cef_shell::window {

struct AlloyTabTransferTarget final {
  std::string window_id;
  std::string label;
};

/// Per-window projection of valid cross-window targets. The coordinator remains
/// the sole owner of transfer validation and BrowserView reparenting.
class AlloyTabTransferSurface final : public CefMenuButtonDelegate,
                                      public CefMenuModelDelegate {
 public:
  struct Callbacks final {
    std::function<std::vector<AlloyTabTransferTarget>()> targets;
    std::function<bool(const std::string&)> move_to;
  };

  static constexpr int kButtonId = 0xd000;
  static constexpr int kTargetCommandBase = MENU_ID_USER_FIRST + 500;

  AlloyTabTransferSurface(localization::LocaleSnapshot locale,
                          Callbacks callbacks);

  bool Attach(CefRefPtr<CefWindow> window, CefRefPtr<CefPanel> toolbar);
  bool Refresh();
  CefRefPtr<CefView> button() const;
  bool menu_open() const noexcept { return menu_model_ != nullptr; }
  bool Shutdown();

  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  void OnMenuButtonPressed(
      CefRefPtr<CefMenuButton> menu_button, const CefPoint& screen_point,
      CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) override;
  void ExecuteCommand(CefRefPtr<CefMenuModel> menu_model, int command_id,
                      cef_event_flags_t event_flags) override;
  void MenuClosed(CefRefPtr<CefMenuModel> menu_model) override;

 private:
  std::string String(const char* key) const;

  static constexpr std::size_t kMaximumTargets = 8;
  localization::LocaleSnapshot locale_;
  Callbacks callbacks_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefMenuButton> button_;
  CefRefPtr<CefMenuModel> menu_model_;
  std::vector<std::string> target_ids_;
  bool active_ = true;

  IMPLEMENT_REFCOUNTING(AlloyTabTransferSurface);
  DISALLOW_COPY_AND_ASSIGN(AlloyTabTransferSurface);
};

}  // namespace crayon::browser::cef_shell::window
