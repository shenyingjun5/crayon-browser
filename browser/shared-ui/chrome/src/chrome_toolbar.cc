#include "crayon/browser_chrome/chrome_toolbar.h"

namespace crayon::browser_chrome {

bool ChromeToolbar::Bounded(const std::string& value, std::size_t max) {
  return !value.empty() && value.size() <= max;
}

void ChromeToolbar::SetNavigation(bool can_go_back, bool can_go_forward) {
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
}

bool ChromeToolbar::SetAddressDisplay(const std::string& display) {
  if (!Bounded(display, kMaxAddressDisplayLen)) {
    return false;
  }
  address_display_ = display;
  return true;
}

bool ChromeToolbar::SetTabTitle(const std::string& title) {
  if (!Bounded(title, kMaxTabTitleLen)) {
    return false;
  }
  tab_title_ = title;
  return true;
}

void CastButtonModel::SetMediaPresent(bool present) {
  if (!present) {
    // Leaving the window resets to the sticky default; a session
    // cannot outlive its surface here.
    state_ = CastButtonState::kHidden;
    return;
  }
  if (state_ == CastButtonState::kHidden) {
    state_ = CastButtonState::kDisabled;
  }
}

void CastButtonModel::SetBrowserVerifiedEligible(bool eligible) {
  if (state_ == CastButtonState::kHidden) {
    return;  // no media surface: eligibility is meaningless
  }
  if (eligible) {
    if (state_ == CastButtonState::kDisabled) {
      state_ = CastButtonState::kEligible;
    }
  } else {
    // Browser verification withdrawn (navigation, pause, gate deny):
    // any pre-session state falls back to Disabled.
    if (state_ == CastButtonState::kEligible || state_ == CastButtonState::kSelecting) {
      state_ = CastButtonState::kDisabled;
    }
  }
}

bool CastButtonModel::OpenReceiverPicker() {
  if (state_ != CastButtonState::kEligible) {
    return false;  // picker only from a browser-verified eligible state
  }
  state_ = CastButtonState::kSelecting;
  return true;
}

void CastButtonModel::CloseReceiverPicker() {
  if (state_ == CastButtonState::kSelecting) {
    state_ = CastButtonState::kEligible;
  }
}

void CastButtonModel::NotifySessionStarted() {
  if (state_ == CastButtonState::kSelecting) {
    state_ = CastButtonState::kCasting;
  }
}

bool CastButtonModel::RequestStop() {
  if (state_ != CastButtonState::kCasting) {
    return false;
  }
  state_ = CastButtonState::kStopping;
  return true;
}

void CastButtonModel::NotifySessionStopped() {
  if (state_ == CastButtonState::kCasting || state_ == CastButtonState::kStopping) {
    // Falls to Disabled: eligibility must be re-verified after stop.
    state_ = CastButtonState::kDisabled;
  }
}

const char* CastButtonModel::label_key() const {
  switch (state_) {
    case CastButtonState::kHidden:
      return "cast.hidden";
    case CastButtonState::kDisabled:
      return "cast.disabled";
    case CastButtonState::kEligible:
      return "cast.select_receiver";
    case CastButtonState::kSelecting:
      return "cast.selecting";
    case CastButtonState::kCasting:
      return "cast.stop";
    case CastButtonState::kStopping:
      return "cast.stopping";
  }
  return "cast.hidden";
}

bool PageErrorShell::Show(PageErrorKind kind) {
  if (kind == PageErrorKind::kNone) {
    kind_ = PageErrorKind::kNone;
    return true;
  }
  kind_ = kind;
  return true;
}

void PageErrorShell::Dismiss() {
  kind_ = PageErrorKind::kNone;
}

PageErrorAction PageErrorShell::PrimaryAction() const {
  switch (kind_) {
    case PageErrorKind::kNetwork:
      return PageErrorAction::kReload;
    case PageErrorKind::kCrash:
      return PageErrorAction::kReload;
    case PageErrorKind::kBlockedScheme:
      return PageErrorAction::kBack;
    case PageErrorKind::kNone:
      return PageErrorAction::kNone;
  }
  return PageErrorAction::kNone;
}

const char* PageErrorShell::message_key() const {
  switch (kind_) {
    case PageErrorKind::kNetwork:
      return "error.network";
    case PageErrorKind::kCrash:
      return "error.crash";
    case PageErrorKind::kBlockedScheme:
      return "error.blocked_scheme";
    case PageErrorKind::kNone:
      return "";
  }
  return "";
}

}  // namespace crayon::browser_chrome
