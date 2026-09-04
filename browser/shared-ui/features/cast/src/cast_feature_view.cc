#include "crayon/browser_cast_view/cast_feature_view.h"

namespace crayon::browser_cast_view {

void CastFeatureViewModel::SetPageActive(bool active) {
  if (!active) {
    // Losing the page resets everything; sessions cannot outlive the
    // surface here (the real session teardown belongs to MED/SDK).
    state_ = CastFeatureState::kIdle;
    return;
  }
  if (state_ == CastFeatureState::kIdle) {
    state_ = CastFeatureState::kBrowsing;
  }
}

bool CastFeatureViewModel::EligiblePreconditions() const {
  return state_ == CastFeatureState::kBrowsing;
}

void CastFeatureViewModel::SetBrowserVerifiedEligible(bool eligible) {
  if (state_ == CastFeatureState::kIdle) {
    return;  // no page: eligibility facts are meaningless
  }
  if (eligible) {
    if (state_ == CastFeatureState::kBrowsing) {
      state_ = CastFeatureState::kEligible;
    }
  } else {
    // Verification withdrawn: pre-session states collapse; a live
    // session is left to the session layer to tear down.
    if (state_ == CastFeatureState::kEligible || state_ == CastFeatureState::kSelecting) {
      state_ = CastFeatureState::kBrowsing;
    }
  }
}

bool CastFeatureViewModel::OpenPicker() {
  if (state_ != CastFeatureState::kEligible) {
    return false;  // picker only from browser-verified eligibility
  }
  state_ = CastFeatureState::kSelecting;
  return true;
}

void CastFeatureViewModel::ClosePicker() {
  if (state_ == CastFeatureState::kSelecting) {
    state_ = CastFeatureState::kEligible;
  }
}

bool CastFeatureViewModel::AcknowledgeRejection() {
  if (state_ != CastFeatureState::kRejected) {
    return false;
  }
  reject_reason_ = RejectReason::kGeneral;
  state_ = CastFeatureState::kBrowsing;
  return true;
}

bool CastFeatureViewModel::SubmitPolicyOutcome(PolicyOutcome outcome, RejectReason reason) {
  if (state_ != CastFeatureState::kSelecting) {
    return false;  // planning only from the picker with eligibility
  }
  switch (outcome) {
    case PolicyOutcome::kDirect:
    case PolicyOutcome::kRelay:
      state_ = CastFeatureState::kPlanning;
      return true;  // caller reports session start/ended separately
    case PolicyOutcome::kExternalClientHandoff:
      state_ = CastFeatureState::kHandoffConfirm;
      return true;
    case PolicyOutcome::kReject:
      reject_reason_ = reason;
      state_ = CastFeatureState::kRejected;
      return true;
  }
  return false;
}

bool CastFeatureViewModel::ConfirmHandoff() {
  if (state_ != CastFeatureState::kHandoffConfirm) {
    return false;
  }
  state_ = CastFeatureState::kHandoffRequested;
  return true;
}

bool CastFeatureViewModel::CancelHandoff() {
  if (state_ != CastFeatureState::kHandoffConfirm &&
      state_ != CastFeatureState::kHandoffRequested) {
    return false;
  }
  handoff_result_ = HandoffResult::kCancelled;
  state_ = CastFeatureState::kBrowsing;  // eligibility must re-verify
  return true;
}

bool CastFeatureViewModel::DeliverHandoffResult(HandoffResult result) {
  if (state_ != CastFeatureState::kHandoffRequested) {
    return false;
  }
  handoff_result_ = result;
  // No outcome means casting started; every result lands back in
  // Browsing with an explicit terminal label.
  state_ = CastFeatureState::kBrowsing;
  return true;
}

void CastFeatureViewModel::NotifySessionStarted() {
  if (state_ == CastFeatureState::kPlanning) {
    state_ = CastFeatureState::kCasting;
  }
}

void CastFeatureViewModel::NotifySessionEnded() {
  if (state_ == CastFeatureState::kCasting || state_ == CastFeatureState::kPlanning) {
    state_ = CastFeatureState::kBrowsing;  // eligibility re-verify needed
  }
}

const char* CastFeatureViewModel::message_key() const {
  switch (state_) {
    case CastFeatureState::kIdle:
      return "cast.feature.idle";
    case CastFeatureState::kBrowsing:
      return "cast.disabled";
    case CastFeatureState::kEligible:
      return "cast.select_receiver";
    case CastFeatureState::kSelecting:
      return "cast.selecting";
    case CastFeatureState::kPlanning:
      return "cast.planning";
    case CastFeatureState::kCasting:
      return "cast.stop";
    case CastFeatureState::kHandoffConfirm:
      return "cast.handoff.confirm";
    case CastFeatureState::kHandoffRequested:
      return "cast.handoff.requested";
    case CastFeatureState::kRejected:
      switch (reject_reason_) {
        case RejectReason::kDrmProtected:
          return "cast.rejected.drm";
        case RejectReason::kNoRoute:
          return "cast.rejected.no_route";
        case RejectReason::kGateDenied:
        case RejectReason::kGeneral:
          return "cast.rejected";
      }
      return "cast.rejected";
  }
  return "cast.rejected";
}

}  // namespace crayon::browser_cast_view
