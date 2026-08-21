#include "crayon/browser_site_controls/permission_prompt_queue.h"

namespace crayon::browser_site_controls {

namespace {

void SetError(SiteControlError* error, SiteControlError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

bool MatchesPrompt(const PermissionPrompt& prompt,
                   const std::string& origin,
                   PermissionKind kind) {
  return prompt.origin == origin && prompt.kind == kind;
}

}  // namespace

bool PermissionPromptQueue::Enqueue(const std::string& origin,
                                    PermissionKind kind,
                                    std::uint64_t now,
                                    std::uint64_t deadline,
                                    SiteControlError* error) {
  if (!active_ || !detail::IsValidSiteOrigin(origin) || !IsValid(kind)) {
    SetError(error, SiteControlError::kInvalidInput);
    return false;
  }
  if (deadline != 0 && deadline <= now) {
    SetError(error, SiteControlError::kInvalidInput);
    return false;
  }
  for (const PermissionPrompt& prompt : prompts_) {
    if (MatchesPrompt(prompt, origin, kind)) {
      SetError(error, SiteControlError::kInvalidInput);
      return false;  // Duplicate pending pair; no side effects.
    }
  }
  if (prompts_.size() >= kMaxPendingPrompts) {
    SetError(error, SiteControlError::kInvalidInput);
    return false;  // Full queue; no side effects.
  }
  prompts_.push_back(PermissionPrompt{origin, kind, now, deadline});
  return true;
}

bool PermissionPromptQueue::ResolveFront(PromptResolution resolution) {
  if (!active_ || prompts_.empty() || !IsValid(resolution)) {
    return false;
  }
  prompts_.pop_front();
  return true;
}

bool PermissionPromptQueue::Cancel(const std::string& origin,
                                   PermissionKind kind) {
  if (!active_) {
    return false;
  }
  for (auto it = prompts_.begin(); it != prompts_.end(); ++it) {
    if (MatchesPrompt(*it, origin, kind)) {
      prompts_.erase(it);
      return true;
    }
  }
  return false;
}

std::size_t PermissionPromptQueue::ExpireTimedOut(std::uint64_t now) {
  if (!active_) {
    return 0;
  }
  std::size_t removed = 0;
  for (auto it = prompts_.begin(); it != prompts_.end();) {
    if (it->deadline != 0 && it->deadline <= now) {
      it = prompts_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

void PermissionPromptQueue::Shutdown() noexcept {
  active_ = false;
  prompts_.clear();
}

}  // namespace crayon::browser_site_controls
