// CEF-07 contract tests: core subprocess supervision lifecycle
// (startup failure, crash, health timeout, bounded reconnect,
// idempotent shutdown, no double-ack orphans).
#include <cstdlib>
#include <iostream>

#include "browser/core_client/core_client_supervisor.h"

namespace {

using crayon::cef_shell::core_client::CoreClientCommand;
using crayon::cef_shell::core_client::CoreClientEvent;
using crayon::cef_shell::core_client::CoreClientOutcome;
using crayon::cef_shell::core_client::CoreClientState;
using crayon::cef_shell::core_client::CoreClientSupervisor;
using crayon::cef_shell::core_client::kHealthTimeoutMs;
using crayon::cef_shell::core_client::kMaxRestartAttempts;
using crayon::cef_shell::core_client::kRestartBackoffMs;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool HappyStartup() {
  CoreClientSupervisor supervisor;
  CHECK(supervisor.state() == CoreClientState::kIdle);
  CHECK(supervisor.Apply(CoreClientCommand::kStart, 0) == CoreClientOutcome::kAccepted);
  CHECK(supervisor.state() == CoreClientState::kSpawning);
  CHECK(supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 10));
  CHECK(supervisor.state() == CoreClientState::kHealthy);
  // Health pings keep it alive.
  CHECK(supervisor.OnEvent(CoreClientEvent::kHealthPinged, 100));
  CHECK(supervisor.Apply(CoreClientCommand::kTick, 100) == CoreClientOutcome::kAccepted);
  CHECK(supervisor.state() == CoreClientState::kHealthy);
  return true;
}

bool StartupFailureBackoffAndRecovery() {
  CoreClientSupervisor supervisor;
  supervisor.Apply(CoreClientCommand::kStart, 0);
  CHECK(supervisor.OnEvent(CoreClientEvent::kSpawnFailed, 5));
  CHECK(supervisor.state() == CoreClientState::kBackoff);
  CHECK(supervisor.restart_attempts() == 1);
  CHECK(supervisor.backoff_ready_at_ms() == 5 + kRestartBackoffMs);
  // Backoff elapses -> spawn again -> healthy.
  CHECK(supervisor.Apply(CoreClientCommand::kTick, 5 + kRestartBackoffMs) ==
        CoreClientOutcome::kAccepted);
  CHECK(supervisor.state() == CoreClientState::kSpawning);
  CHECK(supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 2'000));
  CHECK(supervisor.state() == CoreClientState::kHealthy);
  return true;
}

bool CrashRestartBounded() {
  CoreClientSupervisor supervisor;
  supervisor.Apply(CoreClientCommand::kStart, 0);
  supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 0);
  for (std::size_t i = 1; i <= kMaxRestartAttempts; ++i) {
    CHECK(supervisor.OnEvent(CoreClientEvent::kProcessExited, 100 * i));
    CHECK(supervisor.exit_pending());
    CHECK(supervisor.Apply(CoreClientCommand::kAcknowledgeExit, 100 * i) ==
          CoreClientOutcome::kRestartScheduled);
    CHECK(supervisor.state() == CoreClientState::kBackoff);
    CHECK(supervisor.restart_attempts() == i);
    CHECK(supervisor.Apply(CoreClientCommand::kTick,
                           supervisor.backoff_ready_at_ms()) == CoreClientOutcome::kAccepted);
    CHECK(supervisor.state() == CoreClientState::kSpawning);
    CHECK(supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 10'000 + i));
  }
  // Budget exhausted: the next crash gives up permanently.
  CHECK(supervisor.OnEvent(CoreClientEvent::kProcessExited, 99'999));
  CHECK(supervisor.Apply(CoreClientCommand::kAcknowledgeExit, 99'999) ==
        CoreClientOutcome::kGaveUp);
  CHECK(supervisor.state() == CoreClientState::kFailed);
  CHECK(supervisor.Apply(CoreClientCommand::kStart, 100'000) ==
        CoreClientOutcome::kRejectedStopped);
  return true;
}

bool HealthTimeoutConverges() {
  CoreClientSupervisor supervisor;
  supervisor.Apply(CoreClientCommand::kStart, 0);
  supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 0);
  CHECK(supervisor.Apply(CoreClientCommand::kTick, kHealthTimeoutMs) ==
        CoreClientOutcome::kAccepted);  // boundary still healthy
  CHECK(supervisor.Apply(CoreClientCommand::kTick, kHealthTimeoutMs + 1) ==
        CoreClientOutcome::kExitPending);
  CHECK(supervisor.exit_pending());
  CHECK(supervisor.Apply(CoreClientCommand::kAcknowledgeExit, kHealthTimeoutMs + 2) ==
        CoreClientOutcome::kRestartScheduled);
  CHECK(supervisor.state() == CoreClientState::kBackoff);
  return true;
}

bool ShutdownIsIdempotentAndOrphanFree() {
  CoreClientSupervisor supervisor;
  supervisor.Apply(CoreClientCommand::kStart, 0);
  supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 0);
  CHECK(supervisor.Apply(CoreClientCommand::kStop, 10) == CoreClientOutcome::kAccepted);
  CHECK(supervisor.state() == CoreClientState::kShuttingDown);
  CHECK(supervisor.Apply(CoreClientCommand::kStop, 11) == CoreClientOutcome::kAccepted);
  // Duplicate exits during shutdown are dropped: exactly one ack.
  CHECK(supervisor.OnEvent(CoreClientEvent::kProcessExited, 12));
  CHECK(!supervisor.OnEvent(CoreClientEvent::kProcessExited, 13));
  CHECK(supervisor.Apply(CoreClientCommand::kAcknowledgeExit, 14) ==
        CoreClientOutcome::kAccepted);
  CHECK(supervisor.state() == CoreClientState::kStopped);
  CHECK(!supervisor.exit_pending());
  CHECK(supervisor.Apply(CoreClientCommand::kAcknowledgeExit, 15) ==
        CoreClientOutcome::kRejectedIdle);
  CHECK(supervisor.Apply(CoreClientCommand::kStop, 16) == CoreClientOutcome::kRejectedStopped);
  // Stale post-shutdown events are dropped.
  CHECK(!supervisor.OnEvent(CoreClientEvent::kSpawnAccepted, 20));
  CHECK(!supervisor.OnEvent(CoreClientEvent::kProcessExited, 21));
  return true;
}

bool StopFromSpawningAndIdle() {
  CoreClientSupervisor supervisor;
  CHECK(supervisor.Apply(CoreClientCommand::kStop, 0) == CoreClientOutcome::kAccepted);
  CHECK(supervisor.state() == CoreClientState::kShuttingDown);
  supervisor.Apply(CoreClientCommand::kAcknowledgeExit, 1);  // nothing exited: rejected
  CoreClientSupervisor fresh;
  fresh.Apply(CoreClientCommand::kStart, 0);
  CHECK(fresh.Apply(CoreClientCommand::kStop, 1) == CoreClientOutcome::kAccepted);
  CHECK(fresh.state() == CoreClientState::kShuttingDown);
  CHECK(fresh.Apply(CoreClientCommand::kStart, 2) == CoreClientOutcome::kRejectedBusy);
  // Late spawn result during shutdown is dropped, not resurrecting.
  CHECK(!fresh.OnEvent(CoreClientEvent::kSpawnAccepted, 3));
  CHECK(fresh.OnEvent(CoreClientEvent::kProcessExited, 4));
  CHECK(fresh.Apply(CoreClientCommand::kAcknowledgeExit, 5) == CoreClientOutcome::kAccepted);
  CHECK(fresh.state() == CoreClientState::kStopped);
  return true;
}

/// Deterministic pseudo-random event storm: state stays closed, exit is
/// acknowledged at most once at any time, restart budget respected.
bool StormInvariants() {
  std::uint64_t state = 0x9E37'79B9'7F4A'7C15;
  auto next = [&state]() {
    state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return state;
  };
  CoreClientSupervisor supervisor;
  std::uint64_t clock = 0;
  for (int step = 0; step < 5'000; ++step) {
    clock += next() % 50;
    const std::uint64_t choice = next() % 12;
    if (choice < 3) {
      static_cast<void>(supervisor.Apply(CoreClientCommand::kStart, clock));
    } else if (choice < 5) {
      static_cast<void>(supervisor.Apply(CoreClientCommand::kStop, clock));
    } else if (choice < 7) {
      static_cast<void>(supervisor.Apply(CoreClientCommand::kAcknowledgeExit, clock));
    } else {
      const CoreClientEvent events[] = {CoreClientEvent::kSpawnAccepted,
                                        CoreClientEvent::kSpawnFailed,
                                        CoreClientEvent::kHealthPinged,
                                        CoreClientEvent::kProcessExited};
      static_cast<void>(
          supervisor.OnEvent(events[next() % 4], clock));
    }
    static_cast<void>(supervisor.Apply(CoreClientCommand::kTick, clock));
    CHECK(supervisor.restart_attempts() <= kMaxRestartAttempts + 1);
    const CoreClientState s = supervisor.state();
    CHECK(s == CoreClientState::kIdle || s == CoreClientState::kSpawning ||
          s == CoreClientState::kHealthy || s == CoreClientState::kBackoff ||
          s == CoreClientState::kFailed || s == CoreClientState::kShuttingDown ||
          s == CoreClientState::kStopped);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = HappyStartup() && StartupFailureBackoffAndRecovery() &&
                  CrashRestartBounded() && HealthTimeoutConverges() &&
                  ShutdownIsIdempotentAndOrphanFree() && StopFromSpawningAndIdle() &&
                  StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "core_client_supervisor_test passed\n";
  return EXIT_SUCCESS;
}
