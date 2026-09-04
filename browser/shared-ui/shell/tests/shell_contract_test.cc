#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "crayon/browser_engine/types.h"
#include "crayon/browser_shell/command_registry.h"
#include "crayon/browser_shell/shell_state.h"

namespace {

using crayon::browser_engine::BrowserUrl;
using crayon::browser_engine::EngineErrorCode;
using crayon::browser_engine::NavigationEvent;
using crayon::browser_engine::NavigationEventKind;
using crayon::browser_engine::NavigationId;
using crayon::browser_engine::ProfileEvent;
using crayon::browser_engine::ProfileEventKind;
using crayon::browser_engine::ProfileId;
using crayon::browser_engine::TabEvent;
using crayon::browser_engine::TabEventKind;
using crayon::browser_engine::TabId;
using crayon::browser_shell::CommandDispatchResult;
using crayon::browser_shell::CommandOrigin;
using crayon::browser_shell::CommandRegistry;
using crayon::browser_shell::CommandRouting;
using crayon::browser_shell::EngineEventAdapter;
using crayon::browser_shell::FocusArea;
using crayon::browser_shell::NavigationState;
using crayon::browser_shell::ShellCommand;
using crayon::browser_shell::ShellCommandObserver;
using crayon::browser_shell::ShellCommandTarget;
using crayon::browser_shell::ShellState;
using crayon::browser_shell::ShellSurface;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

template <typename T>
T Require(std::optional<T> value) {
  if (!value.has_value()) {
    std::abort();
  }
  return std::move(*value);
}

class RecordingTarget final : public ShellCommandTarget {
 public:
  bool CanExecute(ShellCommand command) const noexcept override {
    return available_ && command != ShellCommand::kNewTab;
  }

  bool Execute(ShellCommand command) override {
    last_command_ = command;
    ++execution_count_;
    return true;
  }

  void set_available(bool available) noexcept { available_ = available; }
  int execution_count() const noexcept { return execution_count_; }
  ShellCommand last_command() const noexcept { return last_command_; }

 private:
  bool available_ = true;
  int execution_count_ = 0;
  ShellCommand last_command_ = ShellCommand::kNewTab;
};

class CallbackTarget final : public ShellCommandTarget {
 public:
  bool CanExecute(ShellCommand) const noexcept override {
    if (on_check) on_check();
    return available;
  }
  bool Execute(ShellCommand) override {
    ++executions;
    if (on_execute) on_execute();
    return succeeds;
  }
  std::function<void()> on_check;
  std::function<void()> on_execute;
  bool available = true;
  bool succeeds = true;
  int executions = 0;
};

class CallbackObserver final : public ShellCommandObserver {
 public:
  void OnCommandAccepted(ShellCommand, CommandOrigin) override {
    ++accepted;
    if (on_accepted) on_accepted();
  }
  std::function<void()> on_accepted;
  int accepted = 0;
};

constexpr std::array<ShellCommand, 14> kCommandCompatibilityVector = {
    ShellCommand::kNewTab,        ShellCommand::kCloseTab,
    ShellCommand::kFocusOmnibox,  ShellCommand::kBack,
    ShellCommand::kForward,       ShellCommand::kReload,
    ShellCommand::kStop,          ShellCommand::kZoomIn,
    ShellCommand::kZoomOut,       ShellCommand::kResetZoom,
    ShellCommand::kOmniboxEdit,   ShellCommand::kOmniboxSubmit,
    ShellCommand::kOmniboxCancel, ShellCommand::kOmniboxNavigate};

bool CommandOwnershipKeepsLegacyAndCustomPathsSeparate() {
  CallbackTarget target;
  CallbackObserver observer;
  CommandRegistry legacy(target, observer);
  std::uint64_t sequence = 0;
  for (const auto command : kCommandCompatibilityVector) {
    CHECK(legacy.Dispatch(command, ++sequence, CommandOrigin::kNativeChrome) ==
          CommandDispatchResult::kPassThrough);
  }
  CHECK(target.executions == 0);
  CHECK(observer.accepted == 14);

  CommandRegistry custom(target, observer, CommandRouting::kCustomShell);
  CHECK(
      custom.Dispatch(ShellCommand::kNewTab, 1, CommandOrigin::kNativeChrome) ==
      CommandDispatchResult::kInvalidCommand);
  CHECK(custom.last_sequence() == 0);
  sequence = 0;
  for (const auto command : kCommandCompatibilityVector) {
    for (const auto origin :
         {CommandOrigin::kProductUi, CommandOrigin::kHostAccelerator}) {
      CHECK(custom.Dispatch(command, ++sequence, origin) ==
            CommandDispatchResult::kExecuted);
    }
  }
  CHECK(target.executions == 28);
  CHECK(observer.accepted == 42);
  return true;
}

bool CustomDispatchRejectsInvalidUnavailableAndExhaustedCommands() {
  CallbackTarget target;
  CallbackObserver observer;
  CommandRegistry invalid(target, observer, static_cast<CommandRouting>(999));
  CHECK(invalid.Dispatch(ShellCommand::kBack, 1, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kInvalidCommand);
  CHECK(invalid.last_sequence() == 0);
  CommandRegistry custom(target, observer, CommandRouting::kCustomShell);
  CHECK(custom.Dispatch(static_cast<ShellCommand>(999), 1,
                        CommandOrigin::kProductUi) ==
        CommandDispatchResult::kInvalidCommand);
  CHECK(custom.Dispatch(ShellCommand::kBack, 1,
                        static_cast<CommandOrigin>(999)) ==
        CommandDispatchResult::kInvalidCommand);
  CHECK(custom.last_sequence() == 0);
  CHECK(custom.Dispatch(ShellCommand::kBack, 0, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kStaleSequence);
  target.available = false;
  CHECK(custom.Dispatch(ShellCommand::kBack, 1, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kUnavailable);
  CHECK(target.executions == 0);
  target.available = true;
  target.succeeds = false;
  CHECK(custom.Dispatch(ShellCommand::kBack, 2,
                        CommandOrigin::kHostAccelerator) ==
        CommandDispatchResult::kUnavailable);
  CHECK(target.executions == 1);
  CHECK(observer.accepted == 0);
  target.succeeds = true;
  constexpr auto kLastSequence = std::numeric_limits<std::uint64_t>::max();
  CHECK(custom.Dispatch(ShellCommand::kBack, kLastSequence,
                        CommandOrigin::kProductUi) ==
        CommandDispatchResult::kExecuted);
  CHECK(custom.Dispatch(ShellCommand::kBack, kLastSequence,
                        CommandOrigin::kProductUi) ==
        CommandDispatchResult::kStaleSequence);
  CHECK(custom.Dispatch(ShellCommand::kBack, 1, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kStaleSequence);
  custom.Shutdown();
  custom.Shutdown();
  CHECK(custom.Dispatch(ShellCommand::kBack, 3, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kInactive);
  CHECK(target.executions == 2);
  CHECK(observer.accepted == 1);
  return true;
}

bool ShutdownDuringTargetCallbackDoesNotUseRevokedPointers() {
  for (const auto routing :
       {CommandRouting::kNativeChrome, CommandRouting::kCustomShell}) {
    for (const bool during_check : {false, true}) {
      CallbackTarget target;
      CallbackObserver observer;
      CommandRegistry registry(target, observer, routing);
      auto shutdown = [&registry] { registry.Shutdown(); };
      if (during_check)
        target.on_check = shutdown;
      else
        target.on_execute = shutdown;
      CHECK(registry.Dispatch(ShellCommand::kCloseTab, 1,
                              CommandOrigin::kProductUi) ==
            (during_check ? CommandDispatchResult::kInactive
                          : CommandDispatchResult::kExecuted));
      CHECK(target.executions == (during_check ? 0 : 1));
      CHECK(observer.accepted == 0);
      CHECK(!registry.active());
    }
  }
  return true;
}

bool NestedDispatchCannotInterleaveOrConsumeSequence() {
  for (const int callback_stage : {0, 1, 2}) {
    CallbackTarget target;
    CallbackObserver observer;
    CommandRegistry registry(target, observer, CommandRouting::kCustomShell);
    auto nested_result = CommandDispatchResult::kExecuted;
    const auto nested = [&] {
      nested_result = registry.Dispatch(ShellCommand::kReload, 2,
                                        CommandOrigin::kHostAccelerator);
    };
    if (callback_stage == 0) target.on_check = nested;
    if (callback_stage == 1) target.on_execute = nested;
    if (callback_stage == 2) observer.on_accepted = nested;
    CHECK(
        registry.Dispatch(ShellCommand::kBack, 1, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kExecuted);
    CHECK(nested_result == CommandDispatchResult::kReentrant);
    CHECK(registry.last_sequence() == 1);
    CHECK(target.executions == 1);
    CHECK(observer.accepted == 1);
    target.on_check = nullptr;
    target.on_execute = nullptr;
    observer.on_accepted = [&] { registry.Shutdown(); };
    CHECK(registry.Dispatch(ShellCommand::kReload, 2,
                            CommandOrigin::kHostAccelerator) ==
          CommandDispatchResult::kExecuted);
    CHECK(!registry.active());
    CHECK(observer.accepted == 2);
  }
  return true;
}

bool CommandRegistryRejectsDuplicatesAndSeparatesNativePassThrough() {
  RecordingTarget target;
  ShellState state;
  CommandRegistry registry(target, state);

  CHECK(registry.Dispatch(static_cast<ShellCommand>(999), 1,
                          CommandOrigin::kProductUi) ==
        CommandDispatchResult::kInvalidCommand);
  CHECK(registry.Dispatch(ShellCommand::kBack, 1, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kExecuted);
  CHECK(target.execution_count() == 1);
  CHECK(target.last_command() == ShellCommand::kBack);
  CHECK(registry.Dispatch(ShellCommand::kBack, 1, CommandOrigin::kProductUi) ==
        CommandDispatchResult::kStaleSequence);
  CHECK(target.execution_count() == 1);

  CHECK(registry.Dispatch(ShellCommand::kFocusOmnibox, 2,
                          CommandOrigin::kNativeChrome) ==
        CommandDispatchResult::kPassThrough);
  CHECK(target.execution_count() == 1);
  CHECK(state.focus_area() == FocusArea::kOmnibox);

  CHECK(registry.Dispatch(ShellCommand::kOmniboxEdit, 3,
                          CommandOrigin::kProductUi) ==
        CommandDispatchResult::kExecuted);
  CHECK(target.execution_count() == 2);
  CHECK(target.last_command() == ShellCommand::kOmniboxEdit);

  CHECK(registry.Dispatch(ShellCommand::kOmniboxSubmit, 4,
                          CommandOrigin::kProductUi) ==
        CommandDispatchResult::kExecuted);
  CHECK(target.execution_count() == 3);
  CHECK(target.last_command() == ShellCommand::kOmniboxSubmit);

  CHECK(registry.Dispatch(ShellCommand::kOmniboxCancel, 5,
                          CommandOrigin::kNativeChrome) ==
        CommandDispatchResult::kPassThrough);
  CHECK(target.execution_count() == 3);

  CHECK(registry.Dispatch(ShellCommand::kOmniboxNavigate, 6,
                          CommandOrigin::kProductUi) ==
        CommandDispatchResult::kExecuted);
  CHECK(target.execution_count() == 4);
  CHECK(target.last_command() == ShellCommand::kOmniboxNavigate);

  target.set_available(false);
  CHECK(
      registry.Dispatch(ShellCommand::kReload, 7, CommandOrigin::kProductUi) ==
      CommandDispatchResult::kUnavailable);
  CHECK(target.execution_count() == 4);

  registry.Shutdown();
  CHECK(
      registry.Dispatch(ShellCommand::kReload, 8, CommandOrigin::kProductUi) ==
      CommandDispatchResult::kInactive);
  return true;
}

bool ShellStructureMatchesTheFrozenTwoRowContract() {
  CHECK(crayon::browser_shell::kShellSurfaceOrder.size() == 2);
  CHECK(crayon::browser_shell::kShellSurfaceOrder[0] ==
        ShellSurface::kTabStrip);
  CHECK(crayon::browser_shell::kShellSurfaceOrder[1] ==
        ShellSurface::kNavigationBar);
  CHECK(crayon::browser_shell::kPrimaryFocusOrder.size() == 4);
  CHECK(crayon::browser_shell::kPrimaryFocusOrder[0] == FocusArea::kTabStrip);
  CHECK(crayon::browser_shell::kPrimaryFocusOrder[2] == FocusArea::kOmnibox);
  CHECK(crayon::browser_shell::kPrimaryFocusOrder[3] == FocusArea::kPage);
  return true;
}

bool FocusTokensAndRetiredTabsFailClosed() {
  ShellState state;
  CHECK(state.OnProfileCreated("profile-1"));
  CHECK(state.OnTabCreated("profile-1", "tab-1"));
  CHECK(state.SetFocus(FocusArea::kPage, std::string("tab-1")));
  const auto token = state.CaptureFocusForRestore();
  CHECK(token.has_value());
  CHECK(state.SetFocus(FocusArea::kTemporaryLayer, std::nullopt));
  CHECK(state.RestoreFocus(*token));
  CHECK(state.focus_area() == FocusArea::kPage);

  const auto stale_token = state.CaptureFocusForRestore();
  CHECK(stale_token.has_value());
  CHECK(state.OnTabClosed("tab-1"));
  CHECK(state.OnTabClosed("tab-1"));
  CHECK(!state.RestoreFocus(*stale_token));
  CHECK(!state.OnTabCreated("profile-1", "tab-1"));
  CHECK(state.focus_area() == FocusArea::kNone);
  return true;
}

bool NavigationEventsRejectUnknownAndOldGenerations() {
  ShellState state;
  CHECK(state.OnProfileCreated("profile-1"));
  CHECK(state.OnTabCreated("profile-1", "tab-1"));
  CHECK(!state.OnNavigation("missing", 1, NavigationState::kStarted,
                            "https://example.test/"));
  CHECK(state.OnNavigation("tab-1", 2, NavigationState::kStarted,
                           "https://example.test/two"));
  CHECK(state.OnNavigation("tab-1", 2, NavigationState::kCommitted,
                           "https://example.test/two"));
  CHECK(!state.OnNavigation("tab-1", 1, NavigationState::kCompleted,
                            "https://example.test/old"));
  CHECK(!state.OnNavigation("tab-1", 3, NavigationState::kCompleted,
                            "https://example.test/three"));
  const auto* tab = state.FindTab("tab-1");
  CHECK(tab != nullptr);
  CHECK(tab->navigation_id == 2);
  CHECK(tab->navigation_state == NavigationState::kCommitted);
  return true;
}

bool EngineAdapterIgnoresLateEventsAfterShutdown() {
  ShellState state;
  EngineEventAdapter adapter(state);
  const ProfileId profile =
      Require(ProfileId::TryCreate(std::string("profile-1")));
  const TabId tab = Require(TabId::TryCreate(std::string("tab-1")));
  const BrowserUrl url =
      Require(BrowserUrl::TryParse(std::string("https://example.test/")));

  adapter.OnProfileEvent(ProfileEvent{ProfileEventKind::kCreated, profile});
  adapter.OnTabEvent(TabEvent{TabEventKind::kCreated, profile, tab});
  adapter.OnNavigationEvent(NavigationEvent{NavigationEventKind::kStarted, tab,
                                            NavigationId::FromRaw(7), url,
                                            EngineErrorCode::kNone});
  CHECK(state.FindTab("tab-1") != nullptr);
  CHECK(state.FindTab("tab-1")->navigation_id == 7);

  adapter.Shutdown();
  CHECK(!state.active());
  adapter.OnTabEvent(TabEvent{TabEventKind::kCreated, profile, tab});
  CHECK(state.FindTab("tab-1") == nullptr);
  return true;
}

}  // namespace

int main() {
  if (!CommandOwnershipKeepsLegacyAndCustomPathsSeparate() ||
      !CustomDispatchRejectsInvalidUnavailableAndExhaustedCommands() ||
      !ShutdownDuringTargetCallbackDoesNotUseRevokedPointers() ||
      !NestedDispatchCannotInterleaveOrConsumeSequence() ||
      !ShellStructureMatchesTheFrozenTwoRowContract() ||
      !CommandRegistryRejectsDuplicatesAndSeparatesNativePassThrough() ||
      !FocusTokensAndRetiredTabsFailClosed() ||
      !NavigationEventsRejectUnknownAndOldGenerations() ||
      !EngineAdapterIgnoresLateEventsAfterShutdown()) {
    return 1;
  }
  return 0;
}
