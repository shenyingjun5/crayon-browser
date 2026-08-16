#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_SHELL_COMMAND_ADAPTER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_SHELL_COMMAND_ADAPTER_H_

#include <cstdint>
#include <optional>

#include "browser/window/tab_controller.h"
#include "crayon/browser_shell/command_registry.h"
#include "crayon/browser_shell/shell_state.h"

namespace crayon::browser::cef_shell {

class WindowsShellCommandTarget final
    : public browser_shell::ShellCommandTarget {
 public:
  explicit WindowsShellCommandTarget(
      CefRefPtr<window::TabController> tab_controller)
      : tab_controller_(std::move(tab_controller)) {}

  bool CanExecute(browser_shell::ShellCommand command) const noexcept override;
  bool Execute(browser_shell::ShellCommand command) override;

 private:
  CefRefPtr<window::TabController> tab_controller_;
};

class WindowsShellRuntime final {
 public:
  explicit WindowsShellRuntime(CefRefPtr<window::TabController> tab_controller);

  void ObserveChromeCommand(int command_id);
  void Shutdown() noexcept;

  bool active() const noexcept { return registry_.active(); }
  const browser_shell::ShellState& state() const noexcept { return state_; }

 private:
  static std::optional<browser_shell::ShellCommand> MapChromeCommand(
      int command_id);

  browser_shell::ShellState state_;
  WindowsShellCommandTarget target_;
  browser_shell::CommandRegistry registry_;
  std::uint64_t next_sequence_ = 1;
};

}  // namespace crayon::browser::cef_shell

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_WINDOWS_SHELL_COMMAND_ADAPTER_H_
