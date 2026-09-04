#include "crayon/browser_cast_view/cast_ui_coordinator.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace cast = crayon::browser_cast_view;
namespace chrome = crayon::browser_chrome;

#define CHECK_CAST(condition)                                                  \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

cast::CastUiCoordinator EligibleCoordinator() {
  cast::CastUiCoordinator coordinator;
  coordinator.SetPageActive(true);
  coordinator.SetMediaPresent(true);
  coordinator.SetBrowserVerifiedEligible(true);
  return coordinator;
}

bool BrowserVerdictAndEmptyPicker() {
  cast::CastUiCoordinator coordinator;
  coordinator.SetBrowserVerifiedEligible(true);
  CHECK_CAST(!coordinator.OpenPicker());
  coordinator.SetPageActive(true);
  coordinator.SetMediaPresent(true);
  CHECK_CAST(!coordinator.OpenPicker());
  coordinator.SetBrowserVerifiedEligible(true);
  const auto refresh = coordinator.OpenPicker();
  CHECK_CAST(refresh &&
             refresh->kind == cast::CastUiActionKind::kRefreshReceivers);
  CHECK_CAST(coordinator.receivers().empty());
  CHECK_CAST(coordinator.ReplaceReceivers({}));
  coordinator.CancelPicker();
  coordinator.CancelPicker();
  CHECK_CAST(coordinator.feature().state() ==
             cast::CastFeatureState::kEligible);
  CHECK_CAST(coordinator.button().state() ==
             chrome::CastButtonState::kEligible);
  return true;
}

bool SnapshotIsAtomicBoundedAndStable() {
  auto coordinator = EligibleCoordinator();
  CHECK_CAST(coordinator.OpenPicker());
  const std::vector<cast::ReceiverOption> valid = {
      {"aaaaaaaaaaaaaaaa", "客厅", true},
      {"bbbbbbbbbbbbbbbb", "客厅", false},
  };
  CHECK_CAST(coordinator.ReplaceReceivers(valid));
  CHECK_CAST(coordinator.receivers() == valid);
  const cast::CastUiAction expected_select{
      cast::CastUiActionKind::kSelectReceiver, "bbbbbbbbbbbbbbbb", 0};
  CHECK_CAST(coordinator.SelectReceiver("bbbbbbbbbbbbbbbb") == expected_select);
  CHECK_CAST(!coordinator.SelectReceiver("missing"));

  CHECK_CAST(!coordinator.ReplaceReceivers({{"bad.id", "TV", true}}));
  CHECK_CAST(coordinator.receivers() == valid);
  CHECK_CAST(!coordinator.ReplaceReceivers({{"", "TV", true}}));
  CHECK_CAST(!coordinator.ReplaceReceivers(
      {{"cccccccccccccccc", std::string("TV\0hidden", 9), true}}));
  CHECK_CAST(coordinator.receivers() == valid);
  CHECK_CAST(
      !coordinator.ReplaceReceivers({{"aaaaaaaaaaaaaaaa", "TV 1", true},
                                     {"aaaaaaaaaaaaaaaa", "TV 2", true}}));
  CHECK_CAST(coordinator.receivers() == valid);
  CHECK_CAST(!coordinator.ReplaceReceivers(
      {{"cccccccccccccccc", std::string(cast::kMaxReceiverNameBytes + 1, 'x'),
        true}}));
  CHECK_CAST(coordinator.receivers() == valid);
  std::vector<cast::ReceiverOption> too_many;
  for (std::size_t index = 0; index <= cast::kMaxReceiverOptions; ++index) {
    too_many.push_back({"device_" + std::to_string(index), "TV", true});
  }
  CHECK_CAST(!coordinator.ReplaceReceivers(std::move(too_many)));
  CHECK_CAST(coordinator.receivers() == valid);
  return true;
}

bool SessionAndStopAreFenced() {
  auto coordinator = EligibleCoordinator();
  CHECK_CAST(coordinator.OpenPicker());
  CHECK_CAST(coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kDirect));
  CHECK_CAST(!coordinator.NotifySessionStarted(0));
  CHECK_CAST(coordinator.NotifySessionStarted(7));
  CHECK_CAST(coordinator.feature().state() == cast::CastFeatureState::kCasting);
  CHECK_CAST(coordinator.button().state() == chrome::CastButtonState::kCasting);
  const auto stop = coordinator.RequestStop();
  CHECK_CAST(stop && stop->kind == cast::CastUiActionKind::kStopSession &&
             stop->session_generation == 7);
  CHECK_CAST(!coordinator.RequestStop());
  CHECK_CAST(!coordinator.NotifySessionEnded(6));
  CHECK_CAST(coordinator.NotifySessionEnded(7));
  CHECK_CAST(coordinator.NotifySessionEnded(7));
  CHECK_CAST(coordinator.feature().state() ==
             cast::CastFeatureState::kBrowsing);
  CHECK_CAST(coordinator.button().state() ==
             chrome::CastButtonState::kDisabled);
  CHECK_CAST(!coordinator.active_session_generation());
  CHECK_CAST(!coordinator.NotifySessionStarted(7));

  auto disappeared = EligibleCoordinator();
  CHECK_CAST(disappeared.OpenPicker());
  CHECK_CAST(disappeared.ApplyPolicyOutcome(cast::PolicyOutcome::kRelay));
  CHECK_CAST(disappeared.NotifySessionStarted(8));
  disappeared.SetMediaPresent(false);
  CHECK_CAST(disappeared.button().state() == chrome::CastButtonState::kCasting);
  CHECK_CAST(disappeared.NotifySessionEnded(8));
  CHECK_CAST(disappeared.button().state() == chrome::CastButtonState::kHidden);
  return true;
}

bool RejectAndPageLossCannotFakeCasting() {
  auto rejected = EligibleCoordinator();
  CHECK_CAST(rejected.OpenPicker());
  CHECK_CAST(rejected.ApplyPolicyOutcome(cast::PolicyOutcome::kReject,
                                         cast::RejectReason::kDrmProtected));
  CHECK_CAST(rejected.feature().state() == cast::CastFeatureState::kRejected);
  CHECK_CAST(rejected.button().state() == chrome::CastButtonState::kEligible);
  CHECK_CAST(!rejected.NotifySessionStarted(1));
  // A new explicit click may reopen selection only while Browser proof is
  // still current; it never starts a session or reuses the previous receiver.
  CHECK_CAST(rejected.OpenPicker());
  CHECK_CAST(rejected.receivers().empty());
  CHECK_CAST(!rejected.active_session_generation());
  CHECK_CAST(rejected.ApplyPolicyOutcome(cast::PolicyOutcome::kReject));
  rejected.SetBrowserVerifiedEligible(false);
  CHECK_CAST(!rejected.OpenPicker());
  rejected.SetBrowserVerifiedEligible(true);
  CHECK_CAST(rejected.OpenPicker());
  rejected.SetPageActive(false);
  CHECK_CAST(!rejected.OpenPicker());

  auto active = EligibleCoordinator();
  CHECK_CAST(active.OpenPicker());
  CHECK_CAST(active.ApplyPolicyOutcome(cast::PolicyOutcome::kRelay));
  CHECK_CAST(active.NotifySessionStarted(9));
  active.SetPageActive(false);
  CHECK_CAST(active.feature().state() == cast::CastFeatureState::kIdle);
  CHECK_CAST(active.button().state() == chrome::CastButtonState::kHidden);
  CHECK_CAST(!active.active_session_generation());
  CHECK_CAST(active.NotifySessionEnded(9));
  return true;
}

} // namespace

int main() {
  if (!(BrowserVerdictAndEmptyPicker() && SnapshotIsAtomicBoundedAndStable() &&
        SessionAndStopAreFenced() && RejectAndPageLossCannotFakeCasting())) {
    return EXIT_FAILURE;
  }
  std::cout << "cast_ui_coordinator_test passed\n";
  return EXIT_SUCCESS;
}
