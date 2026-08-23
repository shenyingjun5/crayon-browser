#include "browser/core_client/core_client_supervisor.h"

namespace crayon::cef_shell::core_client {

CoreClientOutcome CoreClientSupervisor::Apply(CoreClientCommand command,
                                              std::uint64_t now_ms) {
  switch (command) {
    case CoreClientCommand::kStart:
      if (state_ == CoreClientState::kStopped || state_ == CoreClientState::kFailed) {
        return CoreClientOutcome::kRejectedStopped;
      }
      if (state_ != CoreClientState::kIdle) {
        return CoreClientOutcome::kRejectedBusy;
      }
      state_ = CoreClientState::kSpawning;
      last_health_ms_ = now_ms;
      return CoreClientOutcome::kAccepted;

    case CoreClientCommand::kStop:
      switch (state_) {
        case CoreClientState::kIdle:
        case CoreClientState::kSpawning:
        case CoreClientState::kHealthy:
        case CoreClientState::kBackoff:
          state_ = CoreClientState::kShuttingDown;
          return CoreClientOutcome::kAccepted;
        case CoreClientState::kShuttingDown:
          return CoreClientOutcome::kAccepted;  // idempotent
        case CoreClientState::kStopped:
        case CoreClientState::kFailed:
          return CoreClientOutcome::kRejectedStopped;
      }
      return CoreClientOutcome::kRejectedIdle;

    case CoreClientCommand::kTick:
      if (state_ == CoreClientState::kHealthy &&
          now_ms - last_health_ms_ > kHealthTimeoutMs) {
        // Health timeout is observed as an exit: the port must kill and
        // reap the child, then the owner acknowledges.
        exit_pending_ = true;
        return CoreClientOutcome::kExitPending;
      }
      if (state_ == CoreClientState::kBackoff && now_ms >= backoff_ready_at_ms_) {
        state_ = CoreClientState::kSpawning;
        last_health_ms_ = now_ms;
        return CoreClientOutcome::kAccepted;
      }
      return CoreClientOutcome::kAccepted;

    case CoreClientCommand::kAcknowledgeExit:
      if (!exit_pending_) {
        return CoreClientOutcome::kRejectedIdle;
      }
      exit_pending_ = false;
      if (state_ == CoreClientState::kShuttingDown) {
        // The port reaped the child; the supervisor is fully stopped.
        state_ = CoreClientState::kStopped;
        return CoreClientOutcome::kAccepted;
      }
      ++restart_attempts_;
      if (restart_attempts_ > kMaxRestartAttempts) {
        state_ = CoreClientState::kFailed;
        return CoreClientOutcome::kGaveUp;
      }
      state_ = CoreClientState::kBackoff;
      backoff_ready_at_ms_ =
          now_ms + kRestartBackoffMs * static_cast<std::uint64_t>(restart_attempts_);
      return CoreClientOutcome::kRestartScheduled;
  }
  return CoreClientOutcome::kRejectedIdle;
}

bool CoreClientSupervisor::OnEvent(CoreClientEvent event, std::uint64_t now_ms) {
  switch (event) {
    case CoreClientEvent::kSpawnAccepted:
      if (state_ != CoreClientState::kSpawning) {
        return false;  // stale spawn result dropped
      }
      state_ = CoreClientState::kHealthy;
      last_health_ms_ = now_ms;
      return true;

    case CoreClientEvent::kSpawnFailed:
      if (state_ != CoreClientState::kSpawning) {
        return false;
      }
      ++restart_attempts_;
      if (restart_attempts_ > kMaxRestartAttempts) {
        state_ = CoreClientState::kFailed;
        return true;
      }
      state_ = CoreClientState::kBackoff;
      backoff_ready_at_ms_ =
          now_ms + kRestartBackoffMs * static_cast<std::uint64_t>(restart_attempts_);
      return true;

    case CoreClientEvent::kHealthPinged:
      if (state_ != CoreClientState::kHealthy) {
        return false;
      }
      last_health_ms_ = now_ms;
      return true;

    case CoreClientEvent::kProcessExited:
      if (exit_pending_) {
        return false;  // duplicate exit dropped: exactly one ack, no orphans
      }
      if (state_ != CoreClientState::kHealthy && state_ != CoreClientState::kSpawning &&
          state_ != CoreClientState::kShuttingDown) {
        return false;  // late exit dropped
      }
      exit_pending_ = true;
      return true;
  }
  return false;
}

}  // namespace crayon::cef_shell::core_client
