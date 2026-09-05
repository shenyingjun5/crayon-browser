#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_WINDOW_COORDINATOR_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_WINDOW_COORDINATOR_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "browser/window/alloy_tab_controller.h"
#ifdef CreateWindow
#undef CreateWindow
#endif
#include "crayon/browser_windows/window_state_machine.h"
#include "crayon/browser_session/session_snapshot.h"
#include "include/cef_browser.h"
#include "include/views/cef_window.h"

namespace crayon::browser::cef_shell::window {

class AlloyWindowCoordinator final {
public:
  struct PopupRequest final {
    std::string window_id;
    std::string opener_window_id;
    std::string url;
  };

  struct Callbacks final {
    std::function<bool(const PopupRequest &)> create_popup;
    std::function<void(const std::string &)> focus_window;
    std::function<bool(const browser_session::SessionWindowSnapshot &)>
        restore_window;
  };

  explicit AlloyWindowCoordinator(Callbacks callbacks);
  ~AlloyWindowCoordinator();

  AlloyWindowCoordinator(const AlloyWindowCoordinator &) = delete;
  AlloyWindowCoordinator &operator=(const AlloyWindowCoordinator &) = delete;

  bool CreatePrimary(std::string window_id,
                     browser_engine::ProfileId profile_id,
                     bool restorable = true);
  std::optional<PopupRequest> RequestPopup(const std::string &opener_window_id,
                                           CefRefPtr<CefBrowser> source_browser,
                                           std::string target_url,
                                           bool user_gesture);
  bool AttachWindow(const std::string &window_id, CefRefPtr<CefWindow> window);
  bool RestoreSession(
      const browser_session::SessionProfileSnapshot &snapshot,
      const browser_engine::ProfileId &profile_id);
  bool FocusWindow(const std::string &window_id);
  std::optional<TabId> MoveTab(const std::string &source_window_id,
                               TabId source_tab_id,
                               const std::string &target_window_id);
  bool BeginCloseWindow(const std::string &window_id, bool force_close);
  bool OnWindowClosed(const std::string &window_id);
  bool Shutdown();

  AlloyTabController *controller(const std::string &window_id) noexcept;
  bool has_window(const std::string &window_id) const noexcept;
  bool is_popup(const std::string &window_id) const;
  std::optional<std::string> opener_of(const std::string &window_id) const;
  std::optional<std::string> focused_window_id() const;
  std::size_t window_count() const noexcept;
  std::optional<browser_session::SessionProfileSnapshot>
  SnapshotSession(const browser_engine::ProfileId &profile_id) const;

private:
  struct Record final {
    browser_engine::ProfileId profile_id;
    std::unique_ptr<AlloyTabController> controller;
    CefRefPtr<CefWindow> window;
    bool closing = false;
    bool restorable = true;
  };

  std::string NextPopupId();

  Callbacks callbacks_;
  browser_windows::WindowStateMachine windows_;
  std::map<std::string, Record> records_;
  std::uint64_t next_popup_id_ = 1;
  bool active_ = true;
  bool dispatching_ = false;
};

} // namespace crayon::browser::cef_shell::window

#endif
