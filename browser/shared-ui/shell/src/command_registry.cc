#include "crayon/browser_shell/command_registry.h"

namespace crayon::browser_shell {

CommandDispatchResult CommandRegistry::Dispatch(ShellCommand command,
                                                std::uint64_t sequence,
                                                CommandOrigin origin) {
  if (!IsValid(command) || !IsValid(origin)) {
    return CommandDispatchResult::kInvalidCommand;
  }
  if (!active_) {
    return CommandDispatchResult::kInactive;
  }
  if (sequence == 0 || sequence <= last_sequence_) {
    return CommandDispatchResult::kStaleSequence;
  }
  last_sequence_ = sequence;

  if (origin == CommandOrigin::kNativeChrome) {
    observer_->OnCommandAccepted(command, origin);
    return CommandDispatchResult::kPassThrough;
  }
  if (!target_->CanExecute(command)) {
    return CommandDispatchResult::kUnavailable;
  }
  if (!target_->Execute(command)) {
    return CommandDispatchResult::kUnavailable;
  }
  observer_->OnCommandAccepted(command, origin);
  return CommandDispatchResult::kExecuted;
}

void CommandRegistry::Shutdown() noexcept {
  active_ = false;
  target_ = nullptr;
  observer_ = nullptr;
}

}  // namespace crayon::browser_shell
