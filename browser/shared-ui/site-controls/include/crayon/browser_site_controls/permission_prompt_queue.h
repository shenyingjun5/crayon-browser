#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "crayon/browser_site_controls/site_controls_state_machine.h"

namespace crayon::browser_site_controls {

/// Capacity bounds.
inline constexpr std::size_t kMaxPendingPrompts = 4;

/// Closed outcomes for a queued permission prompt.
enum class PromptResolution {
  kGrant = 0,
  kDeny,
  kDismiss,
};

constexpr bool IsValid(PromptResolution resolution) noexcept {
  switch (resolution) {
    case PromptResolution::kGrant:
    case PromptResolution::kDeny:
    case PromptResolution::kDismiss:
      return true;
  }
  return false;
}

/// A queued permission prompt.  `deadline == 0` means no timeout.
struct PermissionPrompt final {
  std::string origin;
  PermissionKind kind = PermissionKind::kCamera;
  std::uint64_t enqueued_at = 0;
  std::uint64_t deadline = 0;
};

/// FIFO queue of pending permission prompts for one view.
///
/// All timestamps are caller-injected seconds; the module never reads a
/// clock.  Thread contract: single-threaded, UI thread only.
class PermissionPromptQueue final {
 public:
  PermissionPromptQueue() = default;

  /// Enqueues a prompt.  A duplicate pending (origin, kind) pair and a
  /// full queue are both rejected without side effects.  `deadline == 0`
  /// disables the timeout; otherwise it must lie in the future.
  bool Enqueue(const std::string& origin,
               PermissionKind kind,
               std::uint64_t now,
               std::uint64_t deadline,
               SiteControlError* error = nullptr);

  /// Resolves and removes the front prompt with a closed outcome.
  /// Only the front prompt can be resolved (FIFO).
  bool ResolveFront(PromptResolution resolution);

  /// The front prompt, or nullptr when the queue is empty.
  const PermissionPrompt* front() const noexcept {
    if (prompts_.empty()) {
      return nullptr;
    }
    return &prompts_.front();
  }

  /// Removes a specific pending prompt (request withdrawn / tab closed).
  /// Returns false when no matching prompt is pending.
  bool Cancel(const std::string& origin, PermissionKind kind);

  /// Removes every prompt whose deadline passed relative to `now`.
  /// Returns the number removed.
  std::size_t ExpireTimedOut(std::uint64_t now);

  std::size_t size() const noexcept { return prompts_.size(); }
  bool empty() const noexcept { return prompts_.empty(); }

  /// Clears the queue and rejects every subsequent command.
  void Shutdown() noexcept;

  bool active() const noexcept { return active_; }

 private:
  std::deque<PermissionPrompt> prompts_;
  bool active_ = true;
};

}  // namespace crayon::browser_site_controls
