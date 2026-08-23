// CEF-07: core subprocess lifecycle supervision state machine
// (platform-neutral; the actual spawn/kill belongs to the shell port).
//
// Contract (roadmap): bounded startup, health, crash, shutdown and
// reconnect with no orphans — every exit is acknowledged exactly once,
// restarts are bounded with backoff, and shutdown is idempotent.
// Thread contract: single-threaded, caller-injected clock.
#pragma once

#include <cstddef>
#include <cstdint>

namespace crayon::cef_shell::core_client {

/// Maximum restart attempts after crashes before the supervisor gives
/// up (bounded reconnect).
inline constexpr std::size_t kMaxRestartAttempts = 3;
/// Health-check timeout in milliseconds (injected clock).
inline constexpr std::uint64_t kHealthTimeoutMs = 5'000;
/// Minimum backoff before a restart attempt, in milliseconds.
inline constexpr std::uint64_t kRestartBackoffMs = 1'000;

/// Closed supervisor states.
enum class CoreClientState {
  kIdle = 0,
  kSpawning,
  kHealthy,
  kBackoff,
  kFailed,
  kShuttingDown,
  kStopped,
};

/// Closed lifecycle events reported by the platform port.
enum class CoreClientEvent {
  kSpawnAccepted = 0,
  kSpawnFailed,
  kHealthPinged,
  kProcessExited,  // crash or clean exit; the port must have reaped it
};

/// Closed commands from the owning shell.
enum class CoreClientCommand {
  kStart = 0,
  kStop,
  kTick,  // clock advance for health timeouts and backoff
  kAcknowledgeExit,
};

/// Closed outcome of a command application.
enum class CoreClientOutcome {
  kAccepted = 0,
  kRejectedIdle,
  kRejectedBusy,
  kRejectedStopped,
  kExitPending,       // exit observed, waiting for the owning ack
  kRestartScheduled,  // backoff window started
  kGaveUp,            // bounded restart budget exhausted
};

/// Pure supervision state machine.  The port layer maps outcomes to
/// real spawn/kill calls; this class owns no resources, so "no orphan"
/// reduces to: an exit can be observed once, and Stop in any live state
/// converges to kStopped with the port expected to kill the child.
class CoreClientSupervisor final {
 public:
  /// Applies a command; `now_ms` is the injected clock.
  CoreClientOutcome Apply(CoreClientCommand command, std::uint64_t now_ms);

  /// Feeds a port-side event; returns whether it was consumed (stale or
  /// out-of-state events are dropped, e.g. a late spawn result after a
  /// crash was already handled).
  bool OnEvent(CoreClientEvent event, std::uint64_t now_ms);

  CoreClientState state() const { return state_; }
  std::size_t restart_attempts() const { return restart_attempts_; }
  bool exit_pending() const { return exit_pending_; }
  std::uint64_t backoff_ready_at_ms() const { return backoff_ready_at_ms_; }

 private:
  void ScheduleRestart(std::uint64_t now_ms);

  CoreClientState state_ = CoreClientState::kIdle;
  std::size_t restart_attempts_ = 0;
  bool exit_pending_ = false;
  std::uint64_t last_health_ms_ = 0;
  std::uint64_t backoff_ready_at_ms_ = 0;
};

}  // namespace crayon::cef_shell::core_client
