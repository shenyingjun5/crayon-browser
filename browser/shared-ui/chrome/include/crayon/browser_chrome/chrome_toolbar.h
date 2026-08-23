// CEF-08: shared chrome toolbar view model, cast-button state and page
// error shell (no device wiring — CEF-13 owns the real cast feature).
//
// Cast button contract (PRD/MED-19): disabled by default; only a
// browser-verified playback-eligible fact (CEF-10 gate output) moves it
// out of Disabled — page-reported state never does.  External-client
// handoff contexts never render a "casting" state; they render
// "open external client" and cannot claim casting started.
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <string>

namespace crayon::browser_chrome {

/// Closed cast-button states shared with the toolbar.
enum class CastButtonState {
  kHidden = 0,   // no media surface in the window at all
  kDisabled,     // media present, playback not browser-verified
  kEligible,     // browser-verified playback (CEF-10 allow verdict)
  kSelecting,    // receiver picker open (no session yet)
  kCasting,      // Direct/Relay session active
  kStopping,     // session teardown in flight
};

/// Closed page-error families for the error shell.
enum class PageErrorKind { kNone = 0, kNetwork, kCrash, kBlockedScheme };

/// Closed error-shell actions.
enum class PageErrorAction { kReload = 0, kBack, kNone };

/// Toolbar view model aggregating navigation, omnibox and tab facts
/// the shells already own; this class only normalizes them for
/// rendering and never derives facts on its own.
class ChromeToolbar final {
 public:
  /// Updates navigation affordances from the engine state.
  void SetNavigation(bool can_go_back, bool can_go_forward);

  /// Updates the omnibox display text (already normalized by the
  /// omnibox core; oversize input is rejected here as defense in
  /// depth).
  bool SetAddressDisplay(const std::string& display);

  /// Updates the active tab title (bounded).
  bool SetTabTitle(const std::string& title);

  bool can_go_back() const { return can_go_back_; }
  bool can_go_forward() const { return can_go_forward_; }
  const std::string& address_display() const { return address_display_; }
  const std::string& tab_title() const { return tab_title_; }

 private:
  static bool Bounded(const std::string& value, std::size_t max);

  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  std::string address_display_;
  std::string tab_title_;
};

/// Cast-button state machine.  Transitions are closed; the only way in
/// is a browser-side eligible fact, and hidden/disabled are sticky
/// defaults that page input cannot leave.
class CastButtonModel final {
 public:
  /// Reports whether the window has any media surface at all.
  void SetMediaPresent(bool present);

  /// Browser-verified playback eligibility (CEF-10 verdict).  Feeding
  /// `eligible` while no media is present is ignored.
  void SetBrowserVerifiedEligible(bool eligible);

  /// Receiver picker opened/closed without a session.
  bool OpenReceiverPicker();
  void CloseReceiverPicker();

  /// Session lifecycle from the cast feature layer (CEF-13).
  void NotifySessionStarted();
  void NotifySessionStopped();

  CastButtonState state() const { return state_; }

  /// Localized label key for the current state (closed set; never
  /// renders a "casting" label for handoff contexts — CEF-13 owns
  /// those and reuses cast.open_external_client).
  const char* label_key() const;

 private:
  CastButtonState state_ = CastButtonState::kHidden;
};

/// Page error shell model: closed error family, localized message key
/// and offered actions.
class PageErrorShell final {
 public:
  /// Shows an error page; kNone hides it.
  bool Show(PageErrorKind kind);

  /// Dismisses (navigation happened).
  void Dismiss();

  /// Actions offered for the current error.
  PageErrorAction PrimaryAction() const;

  PageErrorKind kind() const { return kind_; }
  const char* message_key() const;
  bool visible() const { return kind_ != PageErrorKind::kNone; }

 private:
  PageErrorKind kind_ = PageErrorKind::kNone;
};

/// Maximum address display / tab title lengths, in bytes.
inline constexpr std::size_t kMaxAddressDisplayLen = 2'048;
inline constexpr std::size_t kMaxTabTitleLen = 512;

}  // namespace crayon::browser_chrome
