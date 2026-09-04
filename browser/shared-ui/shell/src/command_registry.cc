#include "crayon/browser_shell/command_registry.h"

namespace crayon::browser_shell {
namespace {

class DispatchScope final {
 public:
  explicit DispatchScope(bool& dispatching) : dispatching_(dispatching) {
    dispatching_ = true;
  }
  ~DispatchScope() { dispatching_ = false; }
  DispatchScope(const DispatchScope&) = delete;
  DispatchScope& operator=(const DispatchScope&) = delete;

 private:
  bool& dispatching_;
};

}  // namespace

CommandDispatchResult CommandRegistry::Dispatch(ShellCommand command,
                                                std::uint64_t sequence,
                                                CommandOrigin origin) {
  if (!IsValid(command) || !IsValid(origin) || !IsValid(routing_) ||
      (routing_ == CommandRouting::kCustomShell &&
       origin == CommandOrigin::kNativeChrome)) {
    return CommandDispatchResult::kInvalidCommand;
  }
  if (!active_) {
    return CommandDispatchResult::kInactive;
  }
  if (dispatching_) {
    return CommandDispatchResult::kReentrant;
  }
  if (sequence == 0 || sequence <= last_sequence_) {
    return CommandDispatchResult::kStaleSequence;
  }
  DispatchScope dispatch_scope(dispatching_);
  last_sequence_ = sequence;

  if (origin == CommandOrigin::kNativeChrome) {
    observer_->OnCommandAccepted(command, origin);
    return CommandDispatchResult::kPassThrough;
  }
  const bool can_execute = target_->CanExecute(command);
  if (!active_) {
    return CommandDispatchResult::kInactive;
  }
  if (!can_execute) {
    return CommandDispatchResult::kUnavailable;
  }
  if (!target_->Execute(command)) {
    return CommandDispatchResult::kUnavailable;
  }
  // Execute may close the owning window and revoke both callback pointers.
  // Preserve its accepted result without notifying a destroyed UI observer.
  if (active_) {
    observer_->OnCommandAccepted(command, origin);
  }
  return CommandDispatchResult::kExecuted;
}

void CommandRegistry::Shutdown() noexcept {
  active_ = false;
  target_ = nullptr;
  observer_ = nullptr;
}

}  // namespace crayon::browser_shell
