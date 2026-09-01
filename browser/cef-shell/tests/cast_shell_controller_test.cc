#include "browser/media_host/cast_shell_controller.h"

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace cast = crayon::browser_cast_view;
namespace media_host = crayon::browser::cef_shell::media_host;
namespace mh = crayon::browser::cef_shell::media_host::media_host_ipc;

#define CHECK_CAST(condition)                                             \
  do {                                                                    \
    if (!(condition)) {                                                   \
      std::cerr << "check failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                  \
      return false;                                                       \
    }                                                                     \
  } while (false)

struct CommandLog final {
  std::vector<mh::DiscoveryAction> discovery;
  std::vector<std::pair<std::optional<std::uint64_t>, std::uint16_t>> pages;
  std::vector<std::pair<std::uint64_t, std::string>> starts;
  std::vector<std::uint64_t> stops;
  std::vector<std::string> cast_codes;
  std::vector<std::string> cast_code_request_ids;
  struct Control final {
    std::uint64_t generation = 0;
    mh::CastControlAction action = mh::CastControlAction::kPlay;
    std::optional<std::uint64_t> position;
  };
  std::vector<Control> controls;
  std::vector<std::string> control_request_ids;
  bool accept = true;

  media_host::CastCommandPort Port() {
    return media_host::CastCommandPort{
        [this](mh::DiscoveryAction action) {
          discovery.push_back(action);
          return accept;
        },
        [this](std::optional<std::uint64_t> revision, std::uint16_t offset) {
          pages.emplace_back(revision, offset);
          return accept;
        },
        [this](std::uint64_t candidate, std::string device, bool handoff) {
          if (handoff) return false;
          starts.emplace_back(candidate, std::move(device));
          return accept;
        },
        [this](std::uint64_t generation) {
          stops.push_back(generation);
          return accept;
        },
        [this](std::string cast_code) {
          cast_codes.push_back(std::move(cast_code));
          if (!accept) return std::optional<std::string>{};
          cast_code_request_ids.push_back(
              "code-" + std::to_string(cast_code_request_ids.size() + 1));
          return std::optional<std::string>{cast_code_request_ids.back()};
        },
        [this](std::uint64_t generation, mh::CastControlAction action,
               std::optional<std::uint64_t> position) {
          controls.push_back({generation, action, position});
          if (!accept) return std::optional<std::string>{};
          control_request_ids.push_back(
              "control-" + std::to_string(control_request_ids.size() + 1));
          return std::optional<std::string>{control_request_ids.back()};
        }};
  }
};

media_host::MediaPlanningEvent Candidate(std::optional<std::uint64_t> id) {
  return media_host::MediaPlanningEvent{
      media_host::MediaPlanningEventKind::kCandidate,
      id,
      "fixture.invalid",
      std::nullopt,
      std::nullopt,
      std::nullopt};
}

mh::Device Device(std::string id, std::string name,
                  mh::DeviceState state = mh::DeviceState::kReady) {
  return mh::Device{std::move(id), std::move(name), state, true};
}

mh::Message Page(std::uint64_t revision, std::uint16_t offset,
                 std::optional<std::uint16_t> next,
                 std::vector<mh::Device> devices) {
  return mh::DevicePageReply{"page", revision, offset, next,
                             std::move(devices)};
}

mh::Message Started(std::uint64_t generation, mh::DeliveryRoute route) {
  return mh::StartCastReply{"start",
                            {mh::CastStartKind::kCasting, generation, route,
                             std::nullopt, std::nullopt, std::nullopt}};
}

bool EligibilityAndPaging() {
  CommandLog log;
  media_host::CastShellController controller(log.Port());
  controller.OnNavigation();
  controller.ConsumePlanning({Candidate(41)});
  CHECK_CAST(controller.coordinator().button().state() ==
             crayon::browser_chrome::CastButtonState::kHidden);
  controller.OnBrowserVerifiedMedia();
  CHECK_CAST(controller.coordinator().button().state() ==
             crayon::browser_chrome::CastButtonState::kEligible);
  CHECK_CAST(controller.ActivateCastButton());
  CHECK_CAST(log.discovery ==
             std::vector<mh::DiscoveryAction>{mh::DiscoveryAction::kStart});
  CHECK_CAST(log.pages.size() == 1 && !log.pages[0].first &&
             log.pages[0].second == 0);
  CHECK_CAST(!controller.RefreshReceivers());

  controller.ConsumeCast(
      {Page(9, 0, 2,
            {Device("ready_1", "Living room"),
             Device("offline", "Offline", mh::DeviceState::kOffline)})});
  CHECK_CAST(log.pages.size() == 2 && log.pages[1].first == 9 &&
             log.pages[1].second == 2);
  controller.ConsumeCast(
      {Page(9, 2, std::nullopt, {Device("ready_2", "Bedroom")})});
  CHECK_CAST(!controller.device_page_pending());
  CHECK_CAST(controller.coordinator().receivers().size() == 2);
  CHECK_CAST(controller.SelectReceiver("ready_2"));
  CHECK_CAST(!controller.SelectReceiver("ready_1"));
  CHECK_CAST((log.starts == std::vector<std::pair<std::uint64_t, std::string>>{
                                {41, "ready_2"}}));
  return true;
}

bool DirectStopAndLifecycle() {
  CommandLog log;
  media_host::CastShellController controller(log.Port());
  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(7)});
  CHECK_CAST(controller.ActivateCastButton());
  controller.ConsumeCast(
      {Page(1, 0, std::nullopt, {Device("phone", "Crayon phone")})});
  CHECK_CAST(controller.SelectReceiver("phone"));
  controller.ConsumeCast({Started(5, mh::DeliveryRoute::kDirect)});
  CHECK_CAST(controller.coordinator().active_session_generation() == 5);
  CHECK_CAST(controller.coordinator().button().state() ==
             crayon::browser_chrome::CastButtonState::kCasting);
  controller.ConsumeCast({mh::SessionEventsReply{
      "old",
      0,
      {{4, 9, mh::SessionPhase::kTerminated, mh::SessionPlayback::kStopped,
        mh::TerminalReason::kStoppedByReceiver}}}});
  CHECK_CAST(controller.coordinator().active_session_generation() == 5);
  CHECK_CAST(controller.ActivateCastButton());
  CHECK_CAST(log.stops == std::vector<std::uint64_t>{5});

  controller.ConsumeCast({mh::SessionEventsReply{
      "poll",
      0,
      {{5, 1, mh::SessionPhase::kTerminated, mh::SessionPlayback::kStopped,
        mh::TerminalReason::kStoppedBySender}}}});
  CHECK_CAST(!controller.coordinator().active_session_generation());

  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(8)});
  CHECK_CAST(controller.ActivateCastButton());
  controller.ConsumeCast(
      {Page(2, 0, std::nullopt, {Device("phone", "Crayon phone")})});
  CHECK_CAST(controller.SelectReceiver("phone"));
  controller.ConsumeCast({Started(6, mh::DeliveryRoute::kRelay)});
  controller.OnNavigation();
  CHECK_CAST(log.stops == std::vector<std::uint64_t>({5, 6}));
  CHECK_CAST(!controller.coordinator().active_session_generation());
  CHECK_CAST(controller.coordinator().button().state() ==
             crayon::browser_chrome::CastButtonState::kHidden);

  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(9)});
  CHECK_CAST(controller.ActivateCastButton());
  controller.ConsumeCast(
      {Page(3, 0, std::nullopt, {Device("phone", "Crayon phone")})});
  CHECK_CAST(controller.SelectReceiver("phone"));
  controller.ConsumeCast({Started(7, mh::DeliveryRoute::kDirect)});
  log.accept = false;
  CHECK_CAST(!controller.StopSession());
  CHECK_CAST(!controller.coordinator().active_session_generation());
  return true;
}

bool ClosedOutcomesAndFailures() {
  CommandLog log;
  media_host::CastShellController controller(log.Port());
  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(11)});
  CHECK_CAST(controller.ActivateCastButton());
  controller.ConsumeCast(
      {Page(3, 0, std::nullopt, {Device("phone", "Crayon phone")})});
  CHECK_CAST(controller.SelectReceiver("phone"));
  controller.ConsumeCast({mh::StartCastReply{
      "handoff",
      {mh::CastStartKind::kHandoff, std::nullopt, std::nullopt,
       mh::HandoffReason::kNoDirectUrl, std::nullopt, std::nullopt}}});
  CHECK_CAST(!controller.coordinator().active_session_generation());
  CHECK_CAST(controller.coordinator().feature().state() ==
             cast::CastFeatureState::kRejected);

  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(12)});
  CHECK_CAST(controller.ActivateCastButton());
  controller.ConsumeCast({Page(4, 1, std::nullopt, {})});
  CHECK_CAST(controller.coordinator().feature().state() ==
             cast::CastFeatureState::kRejected);

  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(13)});
  log.accept = false;
  CHECK_CAST(!controller.ActivateCastButton());
  CHECK_CAST(controller.coordinator().button().state() ==
             crayon::browser_chrome::CastButtonState::kEligible);
  controller.OnPageClosed();
  controller.Shutdown();
  controller.OnBrowserVerifiedMedia();
  CHECK_CAST(!controller.ActivateCastButton());
  return true;
}

bool RejectAndFailedOutcomes() {
  for (const bool rejected : {true, false}) {
    CommandLog log;
    media_host::CastShellController controller(log.Port());
    controller.OnNavigation();
    controller.OnBrowserVerifiedMedia();
    controller.ConsumePlanning({Candidate(rejected ? 31 : 32)});
    CHECK_CAST(controller.ActivateCastButton());
    controller.ConsumeCast(
        {Page(7, 0, std::nullopt, {Device("phone", "Crayon phone")})});
    CHECK_CAST(controller.SelectReceiver("phone"));
    controller.ConsumeCast({mh::StartCastReply{
        "closed",
        {rejected ? mh::CastStartKind::kRejected : mh::CastStartKind::kFailed,
         std::nullopt, std::nullopt, std::nullopt,
         rejected ? std::optional<mh::CoreError>(mh::CoreError::kDrmProtected)
                  : std::nullopt,
         rejected ? std::nullopt
                  : std::optional<mh::CastError>(
                        mh::CastError::kReceiverUnreachable)}}});
    CHECK_CAST(!controller.coordinator().active_session_generation());
    CHECK_CAST(controller.coordinator().feature().state() ==
               cast::CastFeatureState::kRejected);
    CHECK_CAST(controller.coordinator().feature().reject_reason() ==
               (rejected ? cast::RejectReason::kDrmProtected
                         : cast::RejectReason::kGeneral));
  }
  return true;
}

bool CancelRefreshAndHostFailure() {
  CommandLog log;
  media_host::CastShellController controller(log.Port());
  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(21)});
  CHECK_CAST(controller.ActivateCastButton());
  CHECK_CAST(controller.device_page_pending());
  controller.CancelReceiverPicker();
  CHECK_CAST(!controller.device_page_pending());
  CHECK_CAST(log.discovery.back() == mh::DiscoveryAction::kStop);
  CHECK_CAST(controller.ActivateCastButton());
  controller.ConsumeCast({Page(5, 0, std::nullopt, {})});
  CHECK_CAST(controller.RefreshReceivers());
  CHECK_CAST(log.discovery.back() == mh::DiscoveryAction::kRefresh);
  controller.ConsumeCast({Page(6, 0, std::nullopt, {})});
  controller.CancelReceiverPicker();
  CHECK_CAST(log.discovery.back() == mh::DiscoveryAction::kStop);
  controller.OnHostUnavailable();
  CHECK_CAST(controller.coordinator().button().state() ==
             crayon::browser_chrome::CastButtonState::kHidden);
  return true;
}

bool CastCodeAndPlaybackControls() {
  CommandLog log;
  media_host::CastShellController controller(log.Port());
  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(51)});
  CHECK_CAST(controller.ActivateCastButton());
  log.accept = false;
  CHECK_CAST(!controller.ConnectCastCode("REJECT"));
  CHECK_CAST(controller.presentation().cast_code_failed);
  log.accept = true;
  CHECK_CAST(controller.ConnectCastCode("AB1 CD2"));
  CHECK_CAST(controller.presentation().cast_code_pending);
  CHECK_CAST((log.cast_codes ==
              std::vector<std::string>{"REJECT", "AB1 CD2"}));
  CHECK_CAST(log.discovery.back() == mh::DiscoveryAction::kStop);

  controller.ConsumeCast({mh::ResolveCastCodeReply{
      "code-1", Device("phone", "Crayon phone"), std::nullopt}});
  CHECK_CAST(!controller.presentation().cast_code_pending);
  CHECK_CAST((log.starts == std::vector<std::pair<std::uint64_t, std::string>>{
                                {51, "phone"}}));
  controller.ConsumeCast({Started(15, mh::DeliveryRoute::kDirect)});

  CHECK_CAST(controller.SetPaused(true));
  CHECK_CAST(controller.presentation().control_pending);
  CHECK_CAST(log.controls.back().generation == 15 &&
             log.controls.back().action == mh::CastControlAction::kPause &&
             !log.controls.back().position);
  controller.ConsumeCast(
      {mh::ControlCastReply{"control-1", 15, std::nullopt}});
  CHECK_CAST(controller.presentation().playback_paused);
  CHECK_CAST(!controller.presentation().control_pending);

  CHECK_CAST(controller.SetPaused(false));
  controller.ConsumeCast(
      {mh::ControlCastReply{"control-1", 15, std::nullopt}});
  CHECK_CAST(controller.presentation().control_pending);
  CHECK_CAST(controller.presentation().playback_paused);
  controller.ConsumeCast({mh::ControlCastReply{
      "control-2", 15, mh::CastError::kReceiverUnreachable}});
  CHECK_CAST(controller.presentation().playback_paused);
  CHECK_CAST(controller.presentation().control_failed);
  CHECK_CAST(controller.SeekSession(30));
  CHECK_CAST(log.controls.back().action == mh::CastControlAction::kSeek &&
             log.controls.back().position == 30);
  controller.ConsumeCast(
      {mh::ControlCastReply{"control-3", 14, std::nullopt}});
  CHECK_CAST(controller.presentation().control_pending);
  controller.ConsumeCast(
      {mh::ControlCastReply{"control-2", 15, std::nullopt}});
  CHECK_CAST(controller.presentation().control_pending);
  controller.ConsumeCast(
      {mh::ControlCastReply{"control-3", 15, std::nullopt}});
  CHECK_CAST(!controller.presentation().control_pending);

  controller.OnNavigation();
  controller.OnBrowserVerifiedMedia();
  controller.ConsumePlanning({Candidate(52)});
  CHECK_CAST(controller.ActivateCastButton());
  CHECK_CAST(controller.ConnectCastCode("ZX9"));
  controller.CancelReceiverPicker();
  CHECK_CAST(controller.ActivateCastButton());
  CHECK_CAST(controller.ConnectCastCode("NEXT"));
  controller.ConsumeCast({mh::ResolveCastCodeReply{
      "code-2", Device("late", "Late receiver"), std::nullopt}});
  CHECK_CAST(log.starts.size() == 1);
  CHECK_CAST(controller.presentation().cast_code_pending);
  controller.ConsumeCast({mh::ResolveCastCodeReply{
      "code-3", Device("next", "Next receiver"), std::nullopt}});
  CHECK_CAST(log.starts.size() == 2 && log.starts.back().second == "next");
  return true;
}

}  // namespace

int main() {
  const bool ok = EligibilityAndPaging() && DirectStopAndLifecycle() &&
                  ClosedOutcomesAndFailures() && RejectAndFailedOutcomes() &&
                  CancelRefreshAndHostFailure() &&
                  CastCodeAndPlaybackControls();
  if (ok) std::cout << "cast_shell_controller_test passed\n";
  return ok ? 0 : 1;
}
