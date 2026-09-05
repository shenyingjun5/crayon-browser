// CEF-12 contract tests: merge of media+network observations,
// generation fencing, late-event drops, bounded backpressure, drains.
#include <cstdlib>
#include <iostream>

#include "browser/observation_gateway/observation_gateway.h"

namespace {

using crayon::cef_shell::gateway::EventSource;
using crayon::cef_shell::gateway::GatewayResult;
using crayon::cef_shell::gateway::GatewayStats;
using crayon::cef_shell::gateway::kMaxQueuedEvents;
using crayon::cef_shell::gateway::ObservationGateway;
using crayon::cef_shell::input_proof::PlayerReference;
using crayon::cef_shell::network::NetworkObservation;
using crayon::cef_shell::network::ResourceKind;
using crayon::cef_shell::renderer::MediaObservation;
using crayon::cef_shell::renderer::MediaPlaybackState;
using crayon::cef_shell::renderer::MediaSourceKind;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

MediaObservation Media(std::uint64_t nav) {
  MediaObservation observation;
  observation.navigation_id = nav;
  observation.element_id = 1;
  observation.playback = MediaPlaybackState::kPlaying;
  observation.source_url = "https://a.example/v.mp4";
  observation.source_kind = MediaSourceKind::kHttpUrl;
  observation.visible_fraction = 0.8;
  observation.geometry_supported = true;
  observation.geometry_x = -20;
  observation.geometry_y = 40;
  observation.geometry_width = 320;
  observation.geometry_height = 180;
  observation.viewport_width = 800;
  observation.viewport_height = 600;
  return observation;
}

NetworkObservation Net(std::uint64_t nav) {
  NetworkObservation observation;
  observation.navigation_id = nav;
  observation.url = "https://a.example/seg.ts";
  observation.kind = ResourceKind::kSegment;
  observation.content_length = 5'000;
  return observation;
}

PlayerReference Player(std::uint64_t instance_id = 7,
                       std::uint64_t source_revision = 3) {
  return {instance_id, source_revision};
}

bool MergeAndDrain() {
  ObservationGateway gateway;
  gateway.AdvanceGeneration(/*tab_id=*/1, /*navigation_id=*/10);
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player(), true) ==
        GatewayResult::kAccepted);
  CHECK(gateway.SubmitNetwork(1, 10, Net(10)) == GatewayResult::kAccepted);
  const auto batch = gateway.Drain(10);
  CHECK(batch.size() == 2);
  CHECK(batch[0].source == EventSource::kMedia && batch[0].tab_id == 1);
  CHECK(batch[0].generation == 1);
  CHECK(batch[0].player_reference.has_value());
  CHECK(batch[0].player_reference->instance_id == 7);
  CHECK(batch[0].player_reference->source_revision == 3);
  CHECK(batch[0].media.geometry_supported &&
        batch[0].media.geometry_x == -20 &&
        batch[0].media.viewport_width == 800);
  CHECK(batch[0].eme_encrypted);
  CHECK(batch[1].source == EventSource::kNetwork &&
        !batch[1].player_reference.has_value() &&
        batch[1].media.element_id == 0);
  CHECK(gateway.Drain(10).empty());
  return true;
}

bool GenerationFencingDropsLateEvents() {
  ObservationGateway gateway;
  gateway.AdvanceGeneration(1, 10); // generation 1
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player()) ==
        GatewayResult::kAccepted);
  // Navigation: generation 2; queued generation-1 events drop now.
  CHECK(gateway.AdvanceGeneration(1, 11) == 1);
  CHECK(gateway.stats().queued == 0);
  CHECK(gateway.GenerationOf(1) == 2);
  // A straggler from the old navigation re-submits under the current
  // generation but carries the old navigation id; it merges (the
  // downstream consumer rejects on navigation mismatch), while a tab
  // without any navigation is dropped at the gate.
  // A straggler carrying the OLD navigation id is now rejected at the
  // gateway itself (CEF-12 review follow-up) instead of downstream.
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player()) ==
        GatewayResult::kDroppedStaleGeneration);
  CHECK(gateway.SubmitMedia(2, 10, Media(10), Player()) ==
        GatewayResult::kDroppedStaleGeneration);
  // The current navigation flows.
  CHECK(gateway.SubmitMedia(1, 11, Media(11), Player()) ==
        GatewayResult::kAccepted);
  // Other tabs are unaffected.
  gateway.AdvanceGeneration(3, 30);
  CHECK(gateway.SubmitNetwork(3, 30, Net(30)) == GatewayResult::kAccepted);
  CHECK(gateway.stats().dropped_stale_total == 3);
  return true;
}

bool BackpressureBounded() {
  ObservationGateway gateway;
  gateway.AdvanceGeneration(1, 10);
  for (std::size_t i = 0; i < kMaxQueuedEvents; ++i) {
    CHECK(gateway.SubmitMedia(1, 10, Media(10), Player()) ==
          GatewayResult::kAccepted);
  }
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player()) ==
        GatewayResult::kDroppedBackpressure);
  const GatewayStats stats = gateway.stats();
  CHECK(stats.queued == kMaxQueuedEvents);
  CHECK(stats.dropped_backpressure_total == 1);
  // Draining frees capacity.
  const auto batch = gateway.Drain(kMaxQueuedEvents / 2);
  CHECK(batch.size() == kMaxQueuedEvents / 2);
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player()) ==
        GatewayResult::kAccepted);
  // Zero-drain is a no-op.
  CHECK(gateway.Drain(0).empty());
  return true;
}

bool TabCapacityBounded() {
  ObservationGateway gateway;
  for (std::uint32_t tab = 1; tab <= 64; ++tab) {
    gateway.AdvanceGeneration(tab, tab);
  }
  CHECK(gateway.GenerationOf(64) == 1);
  // The 65th untracked tab cannot fence and its events drop.
  CHECK(gateway.SubmitMedia(65, 1, Media(1), Player()) ==
        GatewayResult::kDroppedStaleGeneration);
  return true;
}

bool InvalidPlayerReferencesAreRejected() {
  ObservationGateway gateway;
  gateway.AdvanceGeneration(1, 10);
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player(0, 1)) ==
        GatewayResult::kDroppedInvalidPlayerReference);
  CHECK(gateway.SubmitMedia(1, 10, Media(10), Player(1, 0)) ==
        GatewayResult::kDroppedInvalidPlayerReference);
  CHECK(gateway.stats().queued == 0);
  CHECK(gateway.stats().dropped_invalid_player_reference_total == 2);
  return true;
}

bool InvalidGeometryIsRejectedBeforeQueueing() {
  ObservationGateway gateway;
  gateway.AdvanceGeneration(1, 10);
  auto media = Media(10);
  media.geometry_supported = false;
  CHECK(gateway.SubmitMedia(1, 10, media, Player()) ==
        GatewayResult::kDroppedInvalidGeometry);
  CHECK(gateway.stats().queued == 0);
  CHECK(gateway.stats().dropped_invalid_geometry_total == 1);
  return true;
}

bool ExactPlayerRemovalIsFencedAndMarked() {
  ObservationGateway gateway;
  gateway.AdvanceGeneration(1, 10);
  auto removed = Media(10);
  removed.geometry_supported = false;
  removed.geometry_x = removed.geometry_y = 0;
  removed.geometry_width = removed.geometry_height = 0;
  removed.viewport_width = removed.viewport_height = 0;
  CHECK(gateway.SubmitPlayerRemoved(1, 10, removed, Player()) ==
        GatewayResult::kAccepted);
  const auto events = gateway.Drain(1);
  CHECK(events.size() == 1);
  CHECK(events[0].source == EventSource::kMedia);
  CHECK(events[0].player_removed);
  CHECK(events[0].player_reference &&
        events[0].player_reference->instance_id == 7 &&
        events[0].player_reference->source_revision == 3);
  removed.navigation_id = 9;
  CHECK(gateway.SubmitPlayerRemoved(1, 9, removed, Player()) ==
        GatewayResult::kDroppedStaleGeneration);
  CHECK(gateway.SubmitPlayerRemoved(1, 10, Media(10), Player(0, 1)) ==
        GatewayResult::kDroppedInvalidPlayerReference);
  return true;
}

/// Pseudo-random storm: queue bounded, counters monotone, drains never
/// exceed the queued amount.
bool StormInvariants() {
  std::uint64_t state = 0x1234'5678'9ABC'DEF0;
  auto next = [&state]() {
    state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return state;
  };
  ObservationGateway gateway;
  std::uint64_t last_backpressure = 0;
  for (int step = 0; step < 5'000; ++step) {
    const std::uint32_t tab = static_cast<std::uint32_t>(next() % 5);
    const std::uint64_t nav = next() % 4;
    switch (next() % 4) {
    case 0:
      static_cast<void>(gateway.AdvanceGeneration(tab, nav));
      break;
    case 1:
      static_cast<void>(gateway.SubmitMedia(tab, nav, Media(nav), Player()));
      break;
    case 2:
      static_cast<void>(gateway.SubmitNetwork(tab, nav, Net(nav)));
      break;
    default:
      static_cast<void>(gateway.Drain(static_cast<std::size_t>(next() % 8)));
      break;
    }
    const GatewayStats stats = gateway.stats();
    CHECK(stats.queued <= kMaxQueuedEvents);
    CHECK(stats.dropped_backpressure_total >= last_backpressure);
    last_backpressure = stats.dropped_backpressure_total;
  }
  return true;
}

} // namespace

int main() {
  const bool ok = MergeAndDrain() && GenerationFencingDropsLateEvents() &&
                  BackpressureBounded() && TabCapacityBounded() &&
                  InvalidPlayerReferencesAreRejected() &&
                  InvalidGeometryIsRejectedBeforeQueueing() &&
                  ExactPlayerRemovalIsFencedAndMarked() && StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "observation_gateway_test passed\n";
  return EXIT_SUCCESS;
}
