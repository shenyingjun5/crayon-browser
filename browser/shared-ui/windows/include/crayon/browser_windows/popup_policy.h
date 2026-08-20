#pragma once

#include <cstddef>

namespace crayon::browser_windows {

/// Maximum number of popup windows a single opener window may own.
inline constexpr std::size_t kMaxPopupsPerWindow = 4;

/// Origin of a popup request.
///
/// `kUserGesture` requests follow a trusted user action (click, key press).
/// `kProgrammatic` requests come from page script without a user gesture and
/// are denied by default, following the minimal-permission principle.
enum class PopupSource {
  kUserGesture = 0,
  kProgrammatic,
};

constexpr bool IsValid(PopupSource source) noexcept {
  switch (source) {
    case PopupSource::kUserGesture:
    case PopupSource::kProgrammatic:
      return true;
  }
  return false;
}

/// Closed decision set for a popup request.
enum class PopupDecision {
  kAllow = 0,
  kDenyNoGesture,       // Programmatic request without user gesture.
  kDenyPopupCap,        // Opener already owns kMaxPopupsPerWindow popups.
  kDenyWindowCap,       // Global window capacity reached.
  kDenyUnknownOpener,   // Opener window does not exist.
  kDenyInvalidRequest,  // Malformed or out-of-domain request.
};

constexpr bool IsAllowed(PopupDecision decision) noexcept {
  return decision == PopupDecision::kAllow;
}

/// Pure popup policy decision.  No state, no I/O, no page data.
///
/// `opener_exists` is supplied by the caller (the window registry owner).
/// `opener_popup_count` is the number of live popups of that opener.
/// `window_cap_reached` reports whether the global window limit is hit.
PopupDecision EvaluatePopupRequest(PopupSource source,
                                   bool opener_exists,
                                   std::size_t opener_popup_count,
                                   bool window_cap_reached) noexcept;

}  // namespace crayon::browser_windows
