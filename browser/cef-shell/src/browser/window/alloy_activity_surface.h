#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "browser/window/alloy_downloads.h"
#include "browser/window/alloy_history.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/cef_menu_model_delegate.h"
#include "include/views/cef_menu_button.h"
#include "include/views/cef_menu_button_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"

namespace crayon::browser::cef_shell::window {

/// Native, bounded history/download menu surface for one Alloy window.
/// Domain owners retain all URLs, paths, state transitions and persistence.
class AlloyActivitySurface final : public CefMenuButtonDelegate,
                                   public CefMenuModelDelegate {
 public:
  struct Callbacks final {
    std::function<bool(const std::string&)> navigate_current;
    std::function<bool()> confirm_clear_history;
    std::function<bool()> persist_history;
    std::function<bool(const std::string&)> confirm_dangerous;
  };

  static constexpr int kHistoryButtonId = 0xce00;
  static constexpr int kDownloadsButtonId = 0xce01;
  static constexpr int kRestoreClosedId = MENU_ID_USER_FIRST + 100;
  static constexpr int kClearHistoryId = MENU_ID_USER_FIRST + 101;
  static constexpr int kHistoryEntryCommandBase = MENU_ID_USER_FIRST + 120;
  static constexpr int kDownloadActionCommandBase = MENU_ID_USER_FIRST + 200;

  AlloyActivitySurface(localization::LocaleSnapshot locale,
                       AlloyHistory* history, AlloyDownloads* downloads,
                       Callbacks callbacks);

  bool Attach(CefRefPtr<CefWindow> window, CefRefPtr<CefPanel> toolbar);
  CefRefPtr<CefView> GetView(int view_id) const;
  bool Shutdown();

  void OnButtonPressed(CefRefPtr<CefButton> button) override;
  void OnMenuButtonPressed(
      CefRefPtr<CefMenuButton> menu_button, const CefPoint& screen_point,
      CefRefPtr<CefMenuButtonPressedLock> button_pressed_lock) override;
  void ExecuteCommand(CefRefPtr<CefMenuModel> menu_model, int command_id,
                      cef_event_flags_t event_flags) override;
  void MenuWillShow(CefRefPtr<CefMenuModel> menu_model) override;
  void MenuClosed(CefRefPtr<CefMenuModel> menu_model) override;

 private:
  enum class DownloadAction {
    kKeep,
    kDiscard,
    kPause,
    kResume,
    kCancel,
    kShowInFolder,
  };

  struct DownloadCommand final {
    std::uint64_t download_id = 0;
    DownloadAction action = DownloadAction::kCancel;
  };

  void BuildHistoryMenu();
  void BuildDownloadsMenu();
  void AddDownloadAction(CefRefPtr<CefMenuModel> menu,
                         std::uint64_t download_id, DownloadAction action,
                         const char* label_key);
  bool ExecuteHistoryEntry(std::size_t index);
  bool ExecuteDownloadAction(std::size_t index);
  std::string DownloadLabel(
      const browser_downloads_view::DownloadProjection& item) const;
  std::string String(const char* key) const;

  localization::LocaleSnapshot locale_;
  AlloyHistory* history_ = nullptr;
  AlloyDownloads* downloads_ = nullptr;
  Callbacks callbacks_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<CefMenuButton> history_button_;
  CefRefPtr<CefMenuButton> downloads_button_;
  CefRefPtr<CefMenuModel> menu_model_;
  std::vector<std::uint64_t> history_entry_ids_;
  std::vector<DownloadCommand> download_commands_;
  bool menu_open_ = false;
  bool active_ = true;

  IMPLEMENT_REFCOUNTING(AlloyActivitySurface);
  DISALLOW_COPY_AND_ASSIGN(AlloyActivitySurface);
};

}  // namespace crayon::browser::cef_shell::window
