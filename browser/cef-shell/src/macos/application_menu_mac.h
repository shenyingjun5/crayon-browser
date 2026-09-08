#pragma once

#include <functional>
#include <memory>
#include <string>

namespace crayon::browser::cef_shell::macos {

enum class ApplicationCommand {
  kAbout, kSettings, kNewTab, kNewWindow, kNewIncognitoWindow, kOpenFile,
  kCloseTab, kSave, kPrint, kFind, kFocusLocation, kReload, kZoomIn,
  kZoomOut, kZoomReset, kBack, kForward, kNextTab, kPreviousTab,
};

// Owns the AppKit menu target for the lifetime of the native application.
// Labels come from the selected CEF locale; actions reuse the browser owner.
class ApplicationMenuMac final {
 public:
  using LabelResolver = std::function<std::string(const char*)>;
  using CommandHandler = std::function<void(ApplicationCommand)>;
  ApplicationMenuMac(std::string app_title, LabelResolver labels,
                     CommandHandler command);
  ~ApplicationMenuMac();
  ApplicationMenuMac(const ApplicationMenuMac&) = delete;
  ApplicationMenuMac& operator=(const ApplicationMenuMac&) = delete;
  bool installed() const noexcept;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace crayon::browser::cef_shell::macos
