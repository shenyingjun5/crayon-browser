#pragma once

// CEF-16: platform-neutral popup routing decision.  No CEF/Win32/AppKit
// types; the shell consumes the closed action set.

#include <cstddef>
#include <string_view>

#include "crayon/browser_windows/popup_policy.h"

namespace crayon::browser::cef_shell::window {

/// Popup target URLs are bounded and http(s)-only; local files keep their
// own gesture-gated entries and must never ride the popup path.
inline constexpr std::size_t kMaxPopupUrlBytes = 2048;

enum class PopupTargetAction {
  kOpenInNewTab = 0, // Cancel the standalone window; open in a new tab.
  kDeny,             // Cancel entirely; no new window and no new tab.
};

constexpr bool IsPopupUrlAllowed(std::string_view url) noexcept {
  constexpr std::string_view kHttp = "http://";
  constexpr std::string_view kHttps = "https://";
  if (url.size() <= kHttps.size() || url.size() > kMaxPopupUrlBytes) {
    return false;
  }
  auto matches = [url](std::string_view prefix) constexpr {
    if (url.size() < prefix.size())
      return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
      char value = url[index];
      if (value >= 'A' && value <= 'Z')
        value = value - 'A' + 'a';
      if (value != prefix[index])
        return false;
    }
    return true;
  };
  const std::size_t authority_start =
      matches(kHttp) ? kHttp.size() : (matches(kHttps) ? kHttps.size() : 0);
  if (authority_start == 0) {
    return false;
  }
  const std::size_t authority_end = url.find_first_of("/?#", authority_start);
  const std::string_view authority =
      url.substr(authority_start, authority_end == std::string_view::npos
                                      ? std::string_view::npos
                                      : authority_end - authority_start);
  if (authority.empty() || authority.find('@') != std::string_view::npos) {
    return false;
  }
  for (const char character : url) {
    const unsigned char byte = static_cast<unsigned char>(character);
    if (byte <= 0x20 || byte == 0x7F) {
      return false;
    }
  }
  return true;
}

/// Routes a popup request through the shared PopupPolicy (gesture/capacity)
/// after URL validation.  `pending_popup_count` is the number of popup URLs
/// already queued for new tabs; `tab_capacity_reached` reports a full tab
/// strip.
inline PopupTargetAction
EvaluatePopupTarget(std::string_view url, bool user_gesture,
                    std::size_t pending_popup_count,
                    bool tab_capacity_reached) noexcept {
  if (!IsPopupUrlAllowed(url)) {
    return PopupTargetAction::kDeny;
  }
  const crayon::browser_windows::PopupDecision decision =
      crayon::browser_windows::EvaluatePopupRequest(
          user_gesture ? crayon::browser_windows::PopupSource::kUserGesture
                       : crayon::browser_windows::PopupSource::kProgrammatic,
          /*opener_exists=*/true, pending_popup_count, tab_capacity_reached);
  return crayon::browser_windows::IsAllowed(decision)
             ? PopupTargetAction::kOpenInNewTab
             : PopupTargetAction::kDeny;
}

} // namespace crayon::browser::cef_shell::window
