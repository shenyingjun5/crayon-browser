#include "crayon/browser_windows/popup_policy.h"

namespace crayon::browser_windows {

PopupDecision EvaluatePopupRequest(PopupSource source,
                                   bool opener_exists,
                                   std::size_t opener_popup_count,
                                   bool window_cap_reached) noexcept {
  if (!IsValid(source)) {
    return PopupDecision::kDenyInvalidRequest;
  }
  if (!opener_exists) {
    return PopupDecision::kDenyUnknownOpener;
  }
  if (source == PopupSource::kProgrammatic) {
    return PopupDecision::kDenyNoGesture;
  }
  if (window_cap_reached) {
    return PopupDecision::kDenyWindowCap;
  }
  if (opener_popup_count >= kMaxPopupsPerWindow) {
    return PopupDecision::kDenyPopupCap;
  }
  return PopupDecision::kAllow;
}

}  // namespace crayon::browser_windows
