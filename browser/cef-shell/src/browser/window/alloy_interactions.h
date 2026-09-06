#pragma once

#include <functional>
#include <string>

#include "crayon/browser_localization/locale_snapshot.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_drag_handler.h"
#include "include/cef_menu_model_delegate.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_menu_button_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"

namespace crayon::browser::cef_shell::window {

enum class AlloyMainCommand {
  kOpenMarkdown = 0,
  kOpenIncognito,
  kCopy,
  kPaste,
  kAbout,
  kLicenses,
};

/// UI-thread-only interaction adapter for one Alloy content view.
class AlloyInteractions final : public CefMenuButtonDelegate,
                                public CefMenuModelDelegate,
                                public CefContextMenuHandler,
                                public CefDragHandler {
public:
  struct Callbacks final {
    std::function<bool(CefRefPtr<CefBrowser>)> open_markdown;
    std::function<bool()> open_incognito;
    std::function<bool(CefRefPtr<CefBrowser>, const std::string &)> navigate;
    std::function<bool(CefRefPtr<CefBrowser>, CefRefPtr<CefDragData>,
                       CefDragHandler::DragOperationsMask)>
        drag_enter;
    std::function<bool(CefRefPtr<CefBrowser>,
                       CefRefPtr<CefContextMenuParams>,
                       CefRefPtr<CefMenuModel>)>
        augment_context_menu;
    std::function<bool(CefRefPtr<CefBrowser>, int)> context_menu_command;
    std::function<void()> cancel_transient;
  };

  static constexpr int kMenuButtonId = 0xcb00;

  AlloyInteractions(localization::LocaleSnapshot locale, Callbacks callbacks);

  bool Attach(CefRefPtr<CefWindow> window, CefRefPtr<CefBrowserView> view,
              CefRefPtr<CefBrowser> browser, CefRefPtr<CefPanel> toolbar);
  bool Execute(AlloyMainCommand command);
  bool HandleAccelerator(int windows_key_code, cef_event_flags_t modifiers);
  bool OnNavigation(CefRefPtr<CefBrowser> browser);
  CefRefPtr<CefView> GetView(int view_id) const;
  bool menu_open() const noexcept { return menu_open_; }
  bool context_menu_active() const noexcept { return context_menu_active_; }
  bool Shutdown();

  void OnMenuButtonPressed(
      CefRefPtr<CefMenuButton> menu_button, const CefPoint &screen_point,
      CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) override;
  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  void ExecuteCommand(CefRefPtr<CefMenuModel> menu_model, int command_id,
                      cef_event_flags_t event_flags) override;
  void MenuWillShow(CefRefPtr<CefMenuModel> menu_model) override;
  void MenuClosed(CefRefPtr<CefMenuModel> menu_model) override;
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;
  void OnContextMenuDismissed(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame) override;
  bool OnDragEnter(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefDragData> drag_data,
                   DragOperationsMask mask) override;

private:
  std::string String(const char *key) const;
  bool IsCurrent(CefRefPtr<CefBrowser> browser) const;

  const localization::LocaleSnapshot locale_;
  Callbacks callbacks_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefMenuButton> menu_button_;
  CefRefPtr<CefMenuModel> menu_model_;
  bool menu_open_ = false;
  bool context_menu_active_ = false;
  bool active_ = true;

  IMPLEMENT_REFCOUNTING(AlloyInteractions);
};

} // namespace crayon::browser::cef_shell::window
