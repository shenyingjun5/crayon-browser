#include "browser/media_host/cast_shell_controller.h"

#include <utility>

namespace crayon::browser::cef_shell::media_host {
namespace {

using browser_cast_view::PolicyOutcome;
using browser_cast_view::RejectReason;

RejectReason MapRejectReason(media_host_ipc::CoreError error) {
  switch (error) {
    case media_host_ipc::CoreError::kDrmProtected:
      return RejectReason::kDrmProtected;
    case media_host_ipc::CoreError::kCapabilitiesUnavailable:
    case media_host_ipc::CoreError::kReceiverIncompatible:
    case media_host_ipc::CoreError::kSessionUnknown:
    case media_host_ipc::CoreError::kSessionExpired:
      return RejectReason::kNoRoute;
    case media_host_ipc::CoreError::kUntrustedObservation:
    case media_host_ipc::CoreError::kMissingUserActivation:
    case media_host_ipc::CoreError::kPlaybackNotAdvanced:
      return RejectReason::kGateDenied;
    default:
      return RejectReason::kGeneral;
  }
}

}  // namespace

CastShellController::CastShellController(CastCommandPort commands)
    : commands_(std::move(commands)) {}

void CastShellController::OnNavigation() {
  if (shutdown_) return;
  StopActiveSession();
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  ResetPage(true);
}

void CastShellController::OnPageClosed() {
  if (shutdown_) return;
  StopActiveSession();
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  ResetPage(false);
}

void CastShellController::OnBrowserVerifiedMedia() {
  if (shutdown_ || !page_active_) return;
  browser_verified_media_ = true;
  coordinator_.SetMediaPresent(true);
  coordinator_.SetBrowserVerifiedEligible(current_candidate_.has_value());
}

void CastShellController::OnHostUnavailable() {
  if (shutdown_) return;
  StopActiveSession();
  ResetPage(page_active_);
}

void CastShellController::Shutdown() {
  if (shutdown_) return;
  StopActiveSession();
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  ResetPage(false);
  shutdown_ = true;
}

void CastShellController::ConsumePlanning(
    std::vector<MediaPlanningEvent> events) {
  if (shutdown_) return;
  for (const auto& event : events) {
    if (event.kind == MediaPlanningEventKind::kCandidate) {
      current_candidate_ = event.candidate_id;
      coordinator_.SetMediaPresent(current_candidate_.has_value() &&
                                   browser_verified_media_);
      coordinator_.SetBrowserVerifiedEligible(current_candidate_.has_value() &&
                                              browser_verified_media_);
    } else if (event.kind == MediaPlanningEventKind::kError) {
      current_candidate_.reset();
      coordinator_.SetBrowserVerifiedEligible(false);
    }
  }
}

void CastShellController::ConsumeCast(
    std::vector<media_host_ipc::Message> messages) {
  if (shutdown_) return;
  for (const auto& message : messages) {
    if (const auto* page =
            std::get_if<media_host_ipc::DevicePageReply>(&message)) {
      if (!device_page_pending_) continue;
      if (!HandleDevicePage(*page)) FailSelection();
    } else if (const auto* start =
                   std::get_if<media_host_ipc::StartCastReply>(&message)) {
      HandleStartReply(*start);
    } else if (const auto* resolved =
                   std::get_if<media_host_ipc::ResolveCastCodeReply>(
                       &message)) {
      HandleResolveCastCodeReply(*resolved);
    } else if (const auto* control =
                   std::get_if<media_host_ipc::ControlCastReply>(&message)) {
      HandleControlCastReply(*control);
    } else if (const auto* events =
                   std::get_if<media_host_ipc::SessionEventsReply>(&message)) {
      HandleSessionEvents(*events);
    } else if (std::holds_alternative<media_host_ipc::ErrorReply>(message)) {
      FailSelection();
    }
  }
}

bool CastShellController::ActivateCastButton() {
  if (shutdown_) return false;
  if (coordinator_.active_session_generation()) return StopSession();
  if (!coordinator_.OpenPicker()) return false;
  if (!RequestFirstDevicePage(media_host_ipc::DiscoveryAction::kStart)) {
    coordinator_.CancelPicker();
    return false;
  }
  return true;
}

bool CastShellController::RefreshReceivers() {
  if (shutdown_ || device_page_pending_ || start_pending_ ||
      cast_code_request_id_)
    return false;
  return RequestFirstDevicePage(media_host_ipc::DiscoveryAction::kRefresh);
}

void CastShellController::CancelReceiverPicker() {
  if (shutdown_ || start_pending_) return;
  coordinator_.CancelPicker();
  pending_receivers_.clear();
  device_snapshot_revision_.reset();
  device_page_pending_ = false;
  cast_code_request_id_.reset();
  cast_code_failed_ = false;
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  discovery_active_ = false;
}

bool CastShellController::SelectReceiver(const std::string &device_id) {
  if (shutdown_ || start_pending_ || cast_code_request_id_ ||
      !current_candidate_ || !commands_.start_cast)
    return false;
  const auto action = coordinator_.SelectReceiver(device_id);
  if (!action ||
      !commands_.start_cast(*current_candidate_, action->device_id, false)) {
    return false;
  }
  start_pending_ = true;
  return true;
}

bool CastShellController::ConnectCastCode(std::string cast_code) {
  if (shutdown_ || cast_code_request_id_ || start_pending_ ||
      !current_candidate_ || !commands_.resolve_cast_code ||
      coordinator_.feature().state() !=
          browser_cast_view::CastFeatureState::kSelecting) {
    return false;
  }
  auto request_id = commands_.resolve_cast_code(std::move(cast_code));
  if (!request_id || request_id->empty()) {
    cast_code_failed_ = true;
    return false;
  }
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  discovery_active_ = false;
  device_page_pending_ = false;
  pending_receivers_.clear();
  device_snapshot_revision_.reset();
  cast_code_request_id_ = std::move(*request_id);
  cast_code_failed_ = false;
  // A pending lookup must not leave an old receiver available for submission.
  static_cast<void>(coordinator_.ReplaceReceivers({}));
  return true;
}

bool CastShellController::SetPaused(bool paused) {
  if (paused == playback_paused_) return false;
  return ControlSession(paused ? media_host_ipc::CastControlAction::kPause
                               : media_host_ipc::CastControlAction::kPlay,
                        std::nullopt);
}

bool CastShellController::SeekSession(std::uint64_t position_seconds) {
  if (position_seconds > media_host_ipc::kMaxSeekSeconds) return false;
  return ControlSession(media_host_ipc::CastControlAction::kSeek,
                        position_seconds);
}

bool CastShellController::StopSession() {
  if (shutdown_ || !commands_.stop_cast) return false;
  const auto action = coordinator_.RequestStop();
  if (!action) return false;
  if (commands_.stop_cast(action->session_generation)) return true;
  static_cast<void>(
      coordinator_.NotifySessionEnded(action->session_generation));
  return false;
}

void CastShellController::ResetPage(bool page_active) {
  coordinator_.SetPageActive(false);
  page_active_ = page_active;
  if (page_active_) coordinator_.SetPageActive(true);
  current_candidate_.reset();
  device_snapshot_revision_.reset();
  pending_receivers_.clear();
  expected_device_offset_ = 0;
  browser_verified_media_ = false;
  discovery_active_ = false;
  device_page_pending_ = false;
  start_pending_ = false;
  cast_code_request_id_.reset();
  cast_code_failed_ = false;
  control_request_id_.reset();
  control_failed_ = false;
  playback_paused_ = false;
  pending_control_action_.reset();
}

void CastShellController::StopActiveSession() {
  if (!coordinator_.active_session_generation() || !commands_.stop_cast) return;
  const auto action = coordinator_.RequestStop();
  if (action)
    static_cast<void>(commands_.stop_cast(action->session_generation));
}

bool CastShellController::RequestFirstDevicePage(
    media_host_ipc::DiscoveryAction action) {
  if (!commands_.discovery || !commands_.list_devices ||
      !commands_.discovery(action)) {
    return false;
  }
  discovery_active_ = true;
  pending_receivers_.clear();
  device_snapshot_revision_.reset();
  expected_device_offset_ = 0;
  if (!commands_.list_devices(std::nullopt, 0)) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
    discovery_active_ = false;
    return false;
  }
  device_page_pending_ = true;
  return true;
}

bool CastShellController::HandleDevicePage(
    const media_host_ipc::DevicePageReply& page) {
  const std::size_t end =
      static_cast<std::size_t>(page.offset) + page.devices.size();
  if (!device_page_pending_ || page.snapshot_revision == 0 ||
      page.devices.size() > media_host_ipc::kMaxDevicePage ||
      end > media_host_ipc::kMaxDevices ||
      (page.next_offset && *page.next_offset != end) ||
      page.offset != expected_device_offset_ ||
      (device_snapshot_revision_ &&
       *device_snapshot_revision_ != page.snapshot_revision)) {
    return false;
  }
  device_snapshot_revision_ = page.snapshot_revision;
  for (const auto& device : page.devices) {
    if (device.state != media_host_ipc::DeviceState::kReady) continue;
    pending_receivers_.push_back(
        {device.device_id, device.display_name, device.is_crayon_receiver});
  }
  if (pending_receivers_.size() > browser_cast_view::kMaxReceiverOptions)
    return false;
  if (page.next_offset) {
    if (!commands_.list_devices ||
        !commands_.list_devices(device_snapshot_revision_, *page.next_offset)) {
      return false;
    }
    expected_device_offset_ = *page.next_offset;
    return true;
  }
  device_page_pending_ = false;
  return coordinator_.ReplaceReceivers(std::move(pending_receivers_));
}

void CastShellController::HandleStartReply(
    const media_host_ipc::StartCastReply& reply) {
  if (!start_pending_) return;
  start_pending_ = false;
  const auto& outcome = reply.outcome;
  if (outcome.kind == media_host_ipc::CastStartKind::kCasting) {
    const PolicyOutcome policy =
        *outcome.route == media_host_ipc::DeliveryRoute::kDirect
            ? PolicyOutcome::kDirect
            : PolicyOutcome::kRelay;
    if (!coordinator_.ApplyPolicyOutcome(policy) ||
        !coordinator_.NotifySessionStarted(*outcome.session_generation)) {
      if (commands_.stop_cast) {
        static_cast<void>(commands_.stop_cast(*outcome.session_generation));
      }
      ResetPage(page_active_);
      return;
    }
    if (discovery_active_ && commands_.discovery) {
      static_cast<void>(
          commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
    }
    discovery_active_ = false;
    control_request_id_.reset();
    control_failed_ = false;
    playback_paused_ = false;
    pending_control_action_.reset();
    return;
  }
  const RejectReason reason =
      outcome.kind == media_host_ipc::CastStartKind::kRejected
          ? MapRejectReason(*outcome.reject_reason)
          : RejectReason::kGeneral;
  static_cast<void>(
      coordinator_.ApplyPolicyOutcome(PolicyOutcome::kReject, reason));
  pending_receivers_.clear();
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  discovery_active_ = false;
}

void CastShellController::HandleResolveCastCodeReply(
    const media_host_ipc::ResolveCastCodeReply &reply) {
  if (!cast_code_request_id_ || reply.request_id != *cast_code_request_id_)
    return;
  cast_code_request_id_.reset();
  if (!reply.device || reply.error ||
      reply.device->state != media_host_ipc::DeviceState::kReady ||
      !coordinator_.ReplaceReceivers(
          {{reply.device->device_id, reply.device->display_name,
            reply.device->is_crayon_receiver}})) {
    cast_code_failed_ = true;
    return;
  }
  // Resolving a code is not permission to play. Keep the picker open until
  // the user's explicit start action calls SelectReceiver.
  cast_code_failed_ = false;
}

void CastShellController::HandleControlCastReply(
    const media_host_ipc::ControlCastReply& reply) {
  const auto generation = coordinator_.active_session_generation();
  if (!control_request_id_ || reply.request_id != *control_request_id_ ||
      !generation || reply.session_generation != *generation)
    return;
  control_request_id_.reset();
  if (reply.error) {
    control_failed_ = true;
    pending_control_action_.reset();
    return;
  }
  if (pending_control_action_ == media_host_ipc::CastControlAction::kPause) {
    playback_paused_ = true;
  } else if (pending_control_action_ ==
             media_host_ipc::CastControlAction::kPlay) {
    playback_paused_ = false;
  }
  control_failed_ = false;
  pending_control_action_.reset();
}

void CastShellController::HandleSessionEvents(
    const media_host_ipc::SessionEventsReply& reply) {
  for (const auto& event : reply.events) {
    const auto generation = coordinator_.active_session_generation();
    if (!generation || event.session_generation != *generation) continue;
    if (event.phase == media_host_ipc::SessionPhase::kTerminated) {
      static_cast<void>(
          coordinator_.NotifySessionEnded(event.session_generation));
      control_request_id_.reset();
      control_failed_ = false;
      playback_paused_ = false;
      pending_control_action_.reset();
    } else if (event.playback == media_host_ipc::SessionPlayback::kPaused) {
      playback_paused_ = true;
    } else if (event.playback == media_host_ipc::SessionPlayback::kPlaying) {
      playback_paused_ = false;
    }
  }
}

bool CastShellController::ControlSession(
    media_host_ipc::CastControlAction action,
    std::optional<std::uint64_t> position_seconds) {
  const auto generation = coordinator_.active_session_generation();
  if (shutdown_ || control_request_id_ || !generation ||
      !commands_.control_cast) {
    return false;
  }
  auto request_id =
      commands_.control_cast(*generation, action, position_seconds);
  if (!request_id || request_id->empty()) return false;
  control_request_id_ = std::move(*request_id);
  control_failed_ = false;
  pending_control_action_ = action;
  return true;
}

CastShellPresentation CastShellController::presentation() const {
  return CastShellPresentation{
      cast_code_request_id_.has_value(), cast_code_failed_,
      control_request_id_.has_value(), control_failed_, playback_paused_};
}

void CastShellController::FailSelection() {
  device_page_pending_ = false;
  start_pending_ = false;
  cast_code_request_id_.reset();
  cast_code_failed_ = true;
  pending_receivers_.clear();
  device_snapshot_revision_.reset();
  if (discovery_active_ && commands_.discovery) {
    static_cast<void>(
        commands_.discovery(media_host_ipc::DiscoveryAction::kStop));
  }
  discovery_active_ = false;
  if (coordinator_.active_session_generation()) {
    StopActiveSession();
    const auto generation = coordinator_.active_session_generation();
    if (generation)
      static_cast<void>(coordinator_.NotifySessionEnded(*generation));
  } else {
    static_cast<void>(coordinator_.ApplyPolicyOutcome(PolicyOutcome::kReject,
                                                      RejectReason::kGeneral));
  }
}

}  // namespace crayon::browser::cef_shell::media_host
