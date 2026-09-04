#include "crayon/browser_cast_view/cast_ui_coordinator.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace crayon::browser_cast_view {
namespace {

bool ValidDeviceId(const std::string &value) {
  return !value.empty() && value.size() <= kMaxReceiverIdBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char byte) {
           return (byte >= '0' && byte <= '9') ||
                  (byte >= 'A' && byte <= 'Z') ||
                  (byte >= 'a' && byte <= 'z') || byte == '_' || byte == '-';
         });
}

bool ValidDisplayName(const std::string &value) {
  return !value.empty() && value.size() <= kMaxReceiverNameBytes &&
         std::none_of(value.begin(), value.end(), [](unsigned char byte) {
           return byte == 0 || byte < 0x20 || byte == 0x7f;
         });
}

} // namespace

void CastUiCoordinator::SetPageActive(bool active) {
  page_active_ = active;
  feature_.SetPageActive(active);
  if (!active) {
    media_present_ = false;
    button_.SetMediaPresent(false);
    receivers_.clear();
    active_session_generation_.reset();
  }
}

void CastUiCoordinator::SetMediaPresent(bool present) {
  media_present_ = page_active_ && present;
  if (!media_present_ && active_session_generation_)
    return;
  button_.SetMediaPresent(media_present_);
  if (!media_present_) {
    feature_.SetBrowserVerifiedEligible(false);
    receivers_.clear();
  }
}

void CastUiCoordinator::SetBrowserVerifiedEligible(bool eligible) {
  const bool verified = page_active_ && media_present_ && eligible;
  feature_.SetBrowserVerifiedEligible(verified);
  button_.SetBrowserVerifiedEligible(verified);
  if (!verified && !active_session_generation_) {
    receivers_.clear();
  }
}

std::optional<CastUiAction> CastUiCoordinator::OpenPicker() {
  // This is an explicit user action, not an automatic policy retry. The
  // button's current Browser-verified eligibility is checked before clearing
  // the error; acknowledging the feature alone cannot grant playback proof.
  if (button_.state() == browser_chrome::CastButtonState::kEligible &&
      feature_.state() == CastFeatureState::kRejected) {
    feature_.AcknowledgeRejection();
    feature_.SetBrowserVerifiedEligible(true);
  }
  if (feature_.state() != CastFeatureState::kEligible ||
      button_.state() != browser_chrome::CastButtonState::kEligible) {
    return std::nullopt;
  }
  if (!feature_.OpenPicker() || !button_.OpenReceiverPicker()) {
    return std::nullopt;
  }
  receivers_.clear();
  return CastUiAction{CastUiActionKind::kRefreshReceivers, {}, 0};
}

void CastUiCoordinator::CancelPicker() {
  if (!PickerOpen())
    return;
  ClearPicker();
}

bool CastUiCoordinator::ReplaceReceivers(
    std::vector<ReceiverOption> receivers) {
  if (!PickerOpen() || receivers.size() > kMaxReceiverOptions)
    return false;
  std::unordered_set<std::string> ids;
  ids.reserve(receivers.size());
  for (const auto &receiver : receivers) {
    if (!ValidReceiver(receiver) || !ids.insert(receiver.device_id).second) {
      return false;
    }
  }
  receivers_ = std::move(receivers);
  return true;
}

std::optional<CastUiAction>
CastUiCoordinator::SelectReceiver(const std::string &device_id) const {
  if (!PickerOpen())
    return std::nullopt;
  const auto receiver =
      std::find_if(receivers_.begin(), receivers_.end(),
                   [&device_id](const ReceiverOption &candidate) {
                     return candidate.device_id == device_id;
                   });
  if (receiver == receivers_.end())
    return std::nullopt;
  return CastUiAction{CastUiActionKind::kSelectReceiver, device_id, 0};
}

bool CastUiCoordinator::ApplyPolicyOutcome(PolicyOutcome outcome,
                                           RejectReason reason) {
  if (!feature_.SubmitPolicyOutcome(outcome, reason))
    return false;
  if (outcome == PolicyOutcome::kExternalClientHandoff ||
      outcome == PolicyOutcome::kReject) {
    button_.CloseReceiverPicker();
    receivers_.clear();
  }
  return true;
}

bool CastUiCoordinator::NotifySessionStarted(std::uint64_t generation) {
  if (generation == 0 || generation <= last_session_generation_ ||
      feature_.state() != CastFeatureState::kPlanning ||
      button_.state() != browser_chrome::CastButtonState::kSelecting) {
    return false;
  }
  feature_.NotifySessionStarted();
  button_.NotifySessionStarted();
  active_session_generation_ = generation;
  last_session_generation_ = generation;
  receivers_.clear();
  return true;
}

std::optional<CastUiAction> CastUiCoordinator::RequestStop() {
  if (!active_session_generation_ || !button_.RequestStop()) {
    return std::nullopt;
  }
  return CastUiAction{
      CastUiActionKind::kStopSession, {}, *active_session_generation_};
}

bool CastUiCoordinator::NotifySessionEnded(std::uint64_t generation) {
  if (generation == 0 || generation < last_session_generation_)
    return false;
  if (!active_session_generation_) {
    return generation == last_session_generation_;
  }
  if (generation != *active_session_generation_)
    return false;
  feature_.NotifySessionEnded();
  button_.NotifySessionStopped();
  if (!media_present_)
    button_.SetMediaPresent(false);
  active_session_generation_.reset();
  receivers_.clear();
  return true;
}

bool CastUiCoordinator::ValidReceiver(const ReceiverOption &receiver) {
  return ValidDeviceId(receiver.device_id) &&
         ValidDisplayName(receiver.display_name);
}

bool CastUiCoordinator::PickerOpen() const {
  return feature_.state() == CastFeatureState::kSelecting &&
         button_.state() == browser_chrome::CastButtonState::kSelecting;
}

void CastUiCoordinator::ClearPicker() {
  feature_.ClosePicker();
  button_.CloseReceiverPicker();
  receivers_.clear();
}

} // namespace crayon::browser_cast_view
