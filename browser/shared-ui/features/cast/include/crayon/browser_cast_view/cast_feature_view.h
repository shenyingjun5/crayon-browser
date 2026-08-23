// CEF-13: cast feature view state machine (no device wiring — SDK-13
// owns real receivers; cast-policy/MED own decisions).
//
// States: Idle/Browsing/Eligible/Selecting/Planning/Casting plus the
// ExternalClientHandoff confirmation flow (MED-19).  Contracts:
//   - Eligible is reachable only from a browser-verified playback fact
//     (the CEF-10 gate verdict); page-reported state cannot reach it.
//   - Handoff requires explicit user confirmation; without it no
//     request is issued, and no outcome ever renders "casting".
//   - Errors are closed and never fake success (Reject/Failed/
//     NotInstalled surface explicit failure keys).
//
// Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstdint>

namespace crayon::browser_cast_view {

/// Closed feature states.
enum class CastFeatureState {
  kIdle = 0,          // no page / feature dormant
  kBrowsing,          // page active, playback not browser-verified
  kEligible,          // browser-verified playback (CEF-10 allow)
  kSelecting,         // receiver picker open
  kPlanning,          // policy decision in flight
  kCasting,           // Direct/Relay session active
  kHandoffConfirm,    // external-client handoff awaiting confirmation
  kHandoffRequested,  // confirmed handoff request issued
  kRejected,          // policy rejected (explicit failure shown)
};

/// Closed policy outcomes (MED-19 decision set, view-side mirror).
enum class PolicyOutcome { kDirect = 0, kRelay, kExternalClientHandoff, kReject };

/// Closed handoff results; none of them means casting started.
enum class HandoffResult { kDownloadStarted = 0, kLaunchRequested, kNotInstalled, kCancelled, kFailed };

/// Closed failure labels for the rejected state.
enum class RejectReason { kGeneral = 0, kDrmProtected, kNoRoute, kGateDenied };

/// View model driving the cast feature surface.
class CastFeatureViewModel final {
 public:
  /// Page activity facts (tab visible/closed).
  void SetPageActive(bool active);

  /// Browser-verified playback eligibility (CEF-10 verdict only).
  void SetBrowserVerifiedEligible(bool eligible);

  /// Receiver picker (CEF-08 CastButtonModel hook).
  bool OpenPicker();
  void ClosePicker();

  /// Submits the (already-decided) policy outcome; illegal states
  /// reject.  kReject/kDirect/kRelay consume the planning result;
  /// kExternalClientHandoff moves to the confirmation flow.
  bool SubmitPolicyOutcome(PolicyOutcome outcome, RejectReason reason = RejectReason::kGeneral);

  /// Handoff confirmation flow.
  bool ConfirmHandoff();
  bool CancelHandoff();
  /// Delivers the closed handoff result; the view never claims casting.
  bool DeliverHandoffResult(HandoffResult result);

  /// Session lifecycle from the cast layer.
  void NotifySessionStarted();
  void NotifySessionEnded();

  CastFeatureState state() const { return state_; }
  RejectReason reject_reason() const { return reject_reason_; }
  HandoffResult last_handoff_result() const { return handoff_result_; }

  /// Localized message key for the current surface (closed set).
  const char* message_key() const;

 private:
  bool EligiblePreconditions() const;

  CastFeatureState state_ = CastFeatureState::kIdle;
  RejectReason reject_reason_ = RejectReason::kGeneral;
  HandoffResult handoff_result_ = HandoffResult::kCancelled;
};

}  // namespace crayon::browser_cast_view
