#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace crayon::browser_shell {

enum class ShellCommand {
  kNewTab = 0,
  kCloseTab,
  kFocusOmnibox,
  kBack,
  kForward,
  kReload,
  kStop,
  kZoomIn,
  kZoomOut,
  kResetZoom,
  kOmniboxEdit,
  kOmniboxSubmit,
  kOmniboxCancel,
  kOmniboxNavigate,
};

enum class CommandOrigin { kProductUi = 0, kNativeChrome, kHostAccelerator };

// Selected by the native composition root, never by page content. This only
// selects command ownership; it does not advertise a working browser backend.
enum class CommandRouting { kNativeChrome = 0, kCustomShell };

enum class CommandDispatchResult {
  kExecuted = 0,
  kPassThrough,
  kUnavailable,
  kInvalidCommand,
  kStaleSequence,
  kInactive,
  kReentrant,
};

enum class FocusArea {
  kNone = 0,
  kTabStrip,
  kNavigation,
  kOmnibox,
  kPage,
  kTemporaryLayer,
};

enum class ShellSurface { kTabStrip = 0, kNavigationBar };

inline constexpr std::array<ShellSurface, 2> kShellSurfaceOrder = {
    ShellSurface::kTabStrip,
    ShellSurface::kNavigationBar,
};

inline constexpr std::array<FocusArea, 4> kPrimaryFocusOrder = {
    FocusArea::kTabStrip,
    FocusArea::kNavigation,
    FocusArea::kOmnibox,
    FocusArea::kPage,
};

constexpr bool IsValid(ShellCommand command) noexcept {
  switch (command) {
    case ShellCommand::kNewTab:
    case ShellCommand::kCloseTab:
    case ShellCommand::kFocusOmnibox:
    case ShellCommand::kBack:
    case ShellCommand::kForward:
    case ShellCommand::kReload:
    case ShellCommand::kStop:
    case ShellCommand::kZoomIn:
    case ShellCommand::kZoomOut:
    case ShellCommand::kResetZoom:
    case ShellCommand::kOmniboxEdit:
    case ShellCommand::kOmniboxSubmit:
    case ShellCommand::kOmniboxCancel:
    case ShellCommand::kOmniboxNavigate:
      return true;
  }
  return false;
}

constexpr bool IsValid(CommandOrigin origin) noexcept {
  switch (origin) {
    case CommandOrigin::kProductUi:
    case CommandOrigin::kNativeChrome:
    case CommandOrigin::kHostAccelerator:
      return true;
  }
  return false;
}

constexpr bool IsValid(CommandRouting routing) noexcept {
  switch (routing) {
    case CommandRouting::kNativeChrome:
    case CommandRouting::kCustomShell:
      return true;
  }
  return false;
}

constexpr bool IsValid(FocusArea area) noexcept {
  switch (area) {
    case FocusArea::kNone:
    case FocusArea::kTabStrip:
    case FocusArea::kNavigation:
    case FocusArea::kOmnibox:
    case FocusArea::kPage:
    case FocusArea::kTemporaryLayer:
      return true;
  }
  return false;
}

struct FocusToken final {
  std::uint64_t generation;
  FocusArea area;
  std::optional<std::string> tab_id;
};

class ShellCommandTarget {
 public:
  virtual ~ShellCommandTarget() = default;

  virtual bool CanExecute(ShellCommand command) const noexcept = 0;
  virtual bool Execute(ShellCommand command) = 0;
};

class ShellCommandObserver {
 public:
  virtual ~ShellCommandObserver() = default;

  virtual void OnCommandAccepted(ShellCommand command,
                                 CommandOrigin origin) = 0;
};

class CommandRegistry final {
 public:
  // UI-thread only. The registry and callback owners must outlive Dispatch.
  // Callbacks may Shutdown(), but must not destroy the registry on its stack.
  // Retain the legacy constructor for existing Chrome-style composition roots.
  CommandRegistry(ShellCommandTarget& target, ShellCommandObserver& observer)
      : CommandRegistry(target, observer, CommandRouting::kNativeChrome) {}
  CommandRegistry(ShellCommandTarget& target, ShellCommandObserver& observer,
                  CommandRouting routing)
      : target_(&target), observer_(&observer), routing_(routing) {}

  CommandRegistry(const CommandRegistry&) = delete;
  CommandRegistry& operator=(const CommandRegistry&) = delete;

  CommandDispatchResult Dispatch(ShellCommand command, std::uint64_t sequence,
                                 CommandOrigin origin);
  void Shutdown() noexcept;

  bool active() const noexcept { return active_; }
  std::uint64_t last_sequence() const noexcept { return last_sequence_; }

 private:
  ShellCommandTarget* target_;
  ShellCommandObserver* observer_;
  const CommandRouting routing_;
  std::uint64_t last_sequence_ = 0;
  bool active_ = true;
  bool dispatching_ = false;
};

}  // namespace crayon::browser_shell
