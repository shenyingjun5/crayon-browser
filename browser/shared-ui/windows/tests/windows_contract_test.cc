#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_windows/popup_policy.h"
#include "crayon/browser_windows/window_state_machine.h"

namespace {

using crayon::browser_windows::IsAllowed;
using crayon::browser_windows::IsValid;
using crayon::browser_windows::kMaxPopupsPerWindow;
using crayon::browser_windows::kMaxWindows;
using crayon::browser_windows::PopupDecision;
using crayon::browser_windows::PopupSource;
using crayon::browser_windows::WindowKind;
using crayon::browser_windows::WindowMode;
using crayon::browser_windows::WindowStateMachine;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// ---------- Enum closure ----------

bool EnumClosureIsComplete() {
  CHECK(IsValid(WindowKind::kNormal));
  CHECK(IsValid(WindowKind::kPopup));
  CHECK(IsValid(WindowMode::kNormal));
  CHECK(IsValid(WindowMode::kFullscreen));
  CHECK(IsValid(WindowMode::kPictureInPicture));
  CHECK(IsValid(PopupSource::kUserGesture));
  CHECK(IsValid(PopupSource::kProgrammatic));
  CHECK(!IsValid(static_cast<WindowKind>(99)));
  CHECK(!IsValid(static_cast<WindowMode>(99)));
  CHECK(!IsValid(static_cast<PopupSource>(99)));
  CHECK(IsAllowed(PopupDecision::kAllow));
  CHECK(!IsAllowed(PopupDecision::kDenyNoGesture));
  return true;
}

// ---------- Window lifecycle ----------

bool CreateAndFocusWindow() {
  WindowStateMachine sm;
  CHECK(sm.CreateWindow("win-1"));
  CHECK(sm.HasWindow("win-1"));
  CHECK(!sm.IsPopup("win-1"));
  CHECK(sm.focused_window_id() == std::optional<std::string>("win-1"));
  CHECK(sm.window_count() == 1);
  return true;
}

bool InvalidAndDuplicateIdsRejected() {
  WindowStateMachine sm;
  CHECK(!sm.CreateWindow(""));
  CHECK(!sm.CreateWindow(std::string(65, 'x')));
  CHECK(sm.CreateWindow(std::string(64, 'x')));
  CHECK(!sm.CreateWindow(std::string(64, 'x')));  // duplicate
  CHECK(sm.window_count() == 1);
  return true;
}

bool WindowCapacityEnforced() {
  WindowStateMachine sm;
  for (std::size_t i = 0; i < kMaxWindows; ++i) {
    CHECK(sm.CreateWindow("win-" + std::to_string(i)));
  }
  CHECK(!sm.CreateWindow("win-overflow"));
  CHECK(sm.window_count() == kMaxWindows);
  return true;
}

bool CloseWindowIsStable() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  sm.CreateWindow("win-2");
  CHECK(sm.CloseWindow("win-1"));
  CHECK(!sm.HasWindow("win-1"));
  CHECK(!sm.CloseWindow("win-1"));    // repeat close
  CHECK(!sm.CloseWindow("missing"));  // unknown
  CHECK(sm.window_count() == 1);
  return true;
}

bool ClosingLastWindowLeavesNoFocus() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  CHECK(sm.CloseWindow("win-1"));
  CHECK(!sm.has_windows());
  CHECK(!sm.focused_window_id().has_value());
  return true;
}

bool FocusFallsBackToRecentNormalWindow() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  sm.CreateWindow("win-2");
  sm.FocusWindow("win-1");
  sm.FocusWindow("win-2");
  CHECK(sm.CloseWindow("win-2"));
  CHECK(sm.focused_window_id() == std::optional<std::string>("win-1"));
  return true;
}

bool FocusUnknownWindowRejected() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  CHECK(!sm.FocusWindow("missing"));
  CHECK(sm.focused_window_id() == std::optional<std::string>("win-1"));
  return true;
}

// ---------- Popup policy ----------

bool ProgrammaticPopupDenied() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  const PopupDecision d =
      sm.RequestPopup("win-1", "pop-1", PopupSource::kProgrammatic);
  CHECK(d == PopupDecision::kDenyNoGesture);
  CHECK(!sm.HasWindow("pop-1"));
  return true;
}

bool UserGesturePopupAllowed() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  const PopupDecision d =
      sm.RequestPopup("win-1", "pop-1", PopupSource::kUserGesture);
  CHECK(d == PopupDecision::kAllow);
  CHECK(sm.IsPopup("pop-1"));
  CHECK(sm.OpenerOf("pop-1") == std::optional<std::string>("win-1"));
  CHECK(sm.focused_window_id() == std::optional<std::string>("pop-1"));
  return true;
}

bool PopupRequiresExistingOpener() {
  WindowStateMachine sm;
  const PopupDecision d =
      sm.RequestPopup("missing", "pop-1", PopupSource::kUserGesture);
  CHECK(d == PopupDecision::kDenyUnknownOpener);
  CHECK(!sm.HasWindow("pop-1"));
  return true;
}

bool PopupInvalidRequestRejected() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  CHECK(sm.RequestPopup("win-1", "", PopupSource::kUserGesture) ==
        PopupDecision::kDenyInvalidRequest);
  CHECK(sm.RequestPopup("win-1", "win-1", PopupSource::kUserGesture) ==
        PopupDecision::kDenyInvalidRequest);  // duplicate id
  CHECK(sm.RequestPopup("win-1", "pop-1",
                        static_cast<PopupSource>(42)) ==
        PopupDecision::kDenyInvalidRequest);
  return true;
}

bool PopupCapPerOpenerEnforced() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  for (std::size_t i = 0; i < kMaxPopupsPerWindow; ++i) {
    CHECK(sm.RequestPopup("win-1", "pop-" + std::to_string(i),
                          PopupSource::kUserGesture) ==
          PopupDecision::kAllow);
  }
  CHECK(sm.RequestPopup("win-1", "pop-extra", PopupSource::kUserGesture) ==
        PopupDecision::kDenyPopupCap);
  return true;
}

bool PopupRespectsGlobalWindowCap() {
  WindowStateMachine sm;
  for (std::size_t i = 0; i < kMaxWindows; ++i) {
    CHECK(sm.CreateWindow("win-" + std::to_string(i)));
  }
  CHECK(sm.RequestPopup("win-0", "pop-1", PopupSource::kUserGesture) ==
        PopupDecision::kDenyWindowCap);
  return true;
}

bool ClosingOpenerKeepsPopup() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  sm.RequestPopup("win-1", "pop-1", PopupSource::kUserGesture);
  CHECK(sm.CloseWindow("win-1"));
  CHECK(sm.HasWindow("pop-1"));
  // Popup focus still defined after opener is gone.
  CHECK(sm.focused_window_id() == std::optional<std::string>("pop-1"));
  return true;
}

// ---------- Fullscreen / picture-in-picture ----------

bool FullscreenEnterExitRestoresNormal() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  CHECK(sm.ModeOf("win-1") == WindowMode::kNormal);
  CHECK(sm.EnterFullscreen("win-1"));
  CHECK(sm.ModeOf("win-1") == WindowMode::kFullscreen);
  CHECK(!sm.EnterFullscreen("win-1"));  // already fullscreen
  CHECK(sm.ExitFullscreen("win-1"));
  CHECK(sm.ModeOf("win-1") == WindowMode::kNormal);
  CHECK(!sm.ExitFullscreen("win-1"));  // not fullscreen anymore
  return true;
}

bool FullscreenAndPipAreMutuallyExclusive() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  CHECK(sm.EnterFullscreen("win-1"));
  CHECK(!sm.EnterPictureInPicture("win-1", true));
  CHECK(sm.ExitFullscreen("win-1"));
  CHECK(sm.EnterPictureInPicture("win-1", true));
  CHECK(!sm.EnterFullscreen("win-1"));
  CHECK(sm.ModeOf("win-1") == WindowMode::kPictureInPicture);
  return true;
}

bool PipRequiresActiveMedia() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  CHECK(!sm.EnterPictureInPicture("win-1", false));
  CHECK(sm.EnterPictureInPicture("win-1", true));
  CHECK(sm.ExitPictureInPicture("win-1"));
  CHECK(!sm.ExitPictureInPicture("win-1"));
  return true;
}

bool PopupWindowsCannotEnterModes() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  sm.RequestPopup("win-1", "pop-1", PopupSource::kUserGesture);
  CHECK(!sm.EnterFullscreen("pop-1"));
  CHECK(!sm.EnterPictureInPicture("pop-1", true));
  CHECK(sm.ModeOf("pop-1") == WindowMode::kNormal);
  return true;
}

bool ModeCommandsOnUnknownWindowRejected() {
  WindowStateMachine sm;
  CHECK(!sm.EnterFullscreen("missing"));
  CHECK(!sm.ExitFullscreen("missing"));
  CHECK(!sm.EnterPictureInPicture("missing", true));
  CHECK(!sm.ExitPictureInPicture("missing"));
  return true;
}

bool ClosingWindowClearsModeState() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  sm.EnterFullscreen("win-1");
  CHECK(sm.CloseWindow("win-1"));
  CHECK(!sm.HasWindow("win-1"));
  // A window re-created with the same ID starts in normal mode.
  CHECK(sm.CreateWindow("win-1"));
  CHECK(sm.ModeOf("win-1") == WindowMode::kNormal);
  return true;
}

// ---------- Shutdown ----------

bool ShutdownRejectsEverything() {
  WindowStateMachine sm;
  sm.CreateWindow("win-1");
  sm.EnterFullscreen("win-1");
  sm.Shutdown();
  CHECK(!sm.active());
  CHECK(sm.window_count() == 0);
  CHECK(!sm.CreateWindow("win-2"));
  CHECK(!sm.CloseWindow("win-1"));
  CHECK(!sm.FocusWindow("win-1"));
  CHECK(sm.RequestPopup("win-1", "pop-1", PopupSource::kUserGesture) ==
        PopupDecision::kDenyInvalidRequest);
  CHECK(!sm.EnterFullscreen("win-1"));
  CHECK(!sm.focused_window_id().has_value());
  return true;
}

// ---------- Pure policy ----------

bool PurePolicyDecisionMatrix() {
  using crayon::browser_windows::EvaluatePopupRequest;
  // Invalid enum fails closed before any other check.
  CHECK(EvaluatePopupRequest(static_cast<PopupSource>(7), false, 0, false) ==
        PopupDecision::kDenyInvalidRequest);
  CHECK(EvaluatePopupRequest(PopupSource::kUserGesture, false, 0, false) ==
        PopupDecision::kDenyUnknownOpener);
  CHECK(EvaluatePopupRequest(PopupSource::kProgrammatic, true, 0, false) ==
        PopupDecision::kDenyNoGesture);
  CHECK(EvaluatePopupRequest(PopupSource::kUserGesture, true, 0, true) ==
        PopupDecision::kDenyWindowCap);
  CHECK(EvaluatePopupRequest(PopupSource::kUserGesture, true,
                             kMaxPopupsPerWindow, false) ==
        PopupDecision::kDenyPopupCap);
  CHECK(EvaluatePopupRequest(PopupSource::kUserGesture, true, 0, false) ==
        PopupDecision::kAllow);
  return true;
}

}  // namespace

int main() {
  if (!EnumClosureIsComplete() || !CreateAndFocusWindow() ||
      !InvalidAndDuplicateIdsRejected() || !WindowCapacityEnforced() ||
      !CloseWindowIsStable() || !ClosingLastWindowLeavesNoFocus() ||
      !FocusFallsBackToRecentNormalWindow() || !FocusUnknownWindowRejected() ||
      !ProgrammaticPopupDenied() || !UserGesturePopupAllowed() ||
      !PopupRequiresExistingOpener() || !PopupInvalidRequestRejected() ||
      !PopupCapPerOpenerEnforced() || !PopupRespectsGlobalWindowCap() ||
      !ClosingOpenerKeepsPopup() || !FullscreenEnterExitRestoresNormal() ||
      !FullscreenAndPipAreMutuallyExclusive() || !PipRequiresActiveMedia() ||
      !PopupWindowsCannotEnterModes() ||
      !ModeCommandsOnUnknownWindowRejected() ||
      !ClosingWindowClearsModeState() || !ShutdownRejectsEverything() ||
      !PurePolicyDecisionMatrix()) {
    return 1;
  }
  return 0;
}
