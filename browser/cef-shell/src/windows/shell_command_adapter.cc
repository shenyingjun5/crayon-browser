#include "windows/shell_command_adapter.h"

#include <limits>
#include <utility>

#include "include/cef_id_mappers.h"

namespace crayon::browser::cef_shell {
namespace {

bool IsCommand(int command_id, const char* command_name) noexcept {
  const int mapped_id = cef_id_for_command_id_name(command_name);
  return mapped_id > 0 && command_id == mapped_id;
}

}  // namespace

bool WindowsShellCommandTarget::CanExecute(
    browser_shell::ShellCommand command) const noexcept {
  if (!tab_controller_ || !browser_shell::IsValid(command)) {
    return false;
  }
  if (command == browser_shell::ShellCommand::kNewTab ||
      command == browser_shell::ShellCommand::kFocusOmnibox) {
    return false;
  }
  const auto active_tab = tab_controller_->model().active_tab();
  return active_tab.has_value() &&
         tab_controller_->model().Find(*active_tab) != nullptr;
}

bool WindowsShellCommandTarget::Execute(browser_shell::ShellCommand command) {
  if (!CanExecute(command)) {
    return false;
  }
  switch (command) {
    case browser_shell::ShellCommand::kCloseTab:
      tab_controller_->CloseActiveTab();
      return true;
    case browser_shell::ShellCommand::kBack:
      tab_controller_->GoBack();
      return true;
    case browser_shell::ShellCommand::kForward:
      tab_controller_->GoForward();
      return true;
    case browser_shell::ShellCommand::kReload:
      tab_controller_->Reload();
      return true;
    case browser_shell::ShellCommand::kStop:
      tab_controller_->Stop();
      return true;
    case browser_shell::ShellCommand::kZoomIn:
      tab_controller_->ZoomIn();
      return true;
    case browser_shell::ShellCommand::kZoomOut:
      tab_controller_->ZoomOut();
      return true;
    case browser_shell::ShellCommand::kResetZoom:
      tab_controller_->ResetZoom();
      return true;
    case browser_shell::ShellCommand::kNewTab:
    case browser_shell::ShellCommand::kFocusOmnibox:
      return false;
  }
  return false;
}

WindowsShellRuntime::WindowsShellRuntime(
    CefRefPtr<window::TabController> tab_controller)
    : target_(std::move(tab_controller)), registry_(target_, state_) {}

void WindowsShellRuntime::ObserveChromeCommand(int command_id) {
  if (!registry_.active()) {
    return;
  }
  const auto command = MapChromeCommand(command_id);
  if (!command.has_value()) {
    return;
  }
  if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    Shutdown();
    return;
  }
  registry_.Dispatch(*command, next_sequence_++,
                     browser_shell::CommandOrigin::kNativeChrome);
}

void WindowsShellRuntime::Shutdown() noexcept {
  registry_.Shutdown();
  state_.Shutdown();
  next_sequence_ = 0;
}

std::optional<browser_shell::ShellCommand>
WindowsShellRuntime::MapChromeCommand(int command_id) {
  using browser_shell::ShellCommand;
  if (IsCommand(command_id, "IDC_NEW_TAB")) {
    return ShellCommand::kNewTab;
  }
  if (IsCommand(command_id, "IDC_CLOSE_TAB")) {
    return ShellCommand::kCloseTab;
  }
  if (IsCommand(command_id, "IDC_FOCUS_LOCATION")) {
    return ShellCommand::kFocusOmnibox;
  }
  if (IsCommand(command_id, "IDC_BACK")) {
    return ShellCommand::kBack;
  }
  if (IsCommand(command_id, "IDC_FORWARD")) {
    return ShellCommand::kForward;
  }
  if (IsCommand(command_id, "IDC_RELOAD") ||
      IsCommand(command_id, "IDC_RELOAD_BYPASSING_CACHE") ||
      IsCommand(command_id, "IDC_RELOAD_CLEARING_CACHE")) {
    return ShellCommand::kReload;
  }
  if (IsCommand(command_id, "IDC_STOP")) {
    return ShellCommand::kStop;
  }
  if (IsCommand(command_id, "IDC_ZOOM_PLUS")) {
    return ShellCommand::kZoomIn;
  }
  if (IsCommand(command_id, "IDC_ZOOM_MINUS")) {
    return ShellCommand::kZoomOut;
  }
  if (IsCommand(command_id, "IDC_ZOOM_NORMAL")) {
    return ShellCommand::kResetZoom;
  }
  return std::nullopt;
}

}  // namespace crayon::browser::cef_shell
