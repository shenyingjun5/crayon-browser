#include "browser/observation_gateway/observation_gateway.h"

#include <algorithm>
#include <utility>

namespace crayon::cef_shell::gateway {
namespace {

constexpr std::size_t kMaxTrackedTabs = 64;

}  // namespace

std::size_t ObservationGateway::AdvanceGeneration(std::uint32_t tab_id) {
  auto it = std::find_if(tabs_.begin(), tabs_.end(),
                         [tab_id](const std::pair<std::uint32_t, TabState>& entry) {
                           return entry.first == tab_id;
                         });
  if (it == tabs_.end()) {
    if (tabs_.size() >= kMaxTrackedTabs) {
      return 0;  // bounded map; untracked tabs cannot fence
    }
    tabs_.emplace_back(tab_id, TabState{});
    it = std::prev(tabs_.end());
  }
  ++it->second.generation;
  const std::uint32_t generation = it->second.generation;
  // Drop every queued event of an older generation for this tab.
  const auto before = queue_.size();
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                              [tab_id, generation](const GatewayEvent& event) {
                                return event.tab_id == tab_id &&
                                       event.generation < generation;
                              }),
               queue_.end());
  dropped_stale_total_ += before - queue_.size();
  return before - queue_.size();
}

std::uint32_t ObservationGateway::GenerationOf(std::uint32_t tab_id) const {
  const auto it = std::find_if(
      tabs_.begin(), tabs_.end(),
      [tab_id](const std::pair<std::uint32_t, TabState>& entry) {
        return entry.first == tab_id;
      });
  return it == tabs_.end() ? 0 : it->second.generation;
}

GatewayResult ObservationGateway::Submit(GatewayEvent event) {
  // Fence first: events stamped with an older generation than the
  // tab's current one are stale (BR-007-shaped stragglers).
  event.generation = GenerationOf(event.tab_id);
  if (event.navigation_id == 0 || event.generation == 0) {
    // No navigation recorded for the tab: nothing can be attributed.
    ++dropped_stale_total_;
    return GatewayResult::kDroppedStaleGeneration;
  }
  if (queue_.size() >= kMaxQueuedEvents) {
    ++dropped_backpressure_total_;
    return GatewayResult::kDroppedBackpressure;
  }
  queue_.push_back(event);
  return GatewayResult::kAccepted;
}

GatewayResult ObservationGateway::SubmitMedia(
    std::uint32_t tab_id, std::uint64_t navigation_id,
    const renderer::MediaObservation& observation) {
  GatewayEvent event;
  event.source = EventSource::kMedia;
  event.tab_id = tab_id;
  event.navigation_id = navigation_id;
  event.media = observation;
  return Submit(event);
}

GatewayResult ObservationGateway::SubmitNetwork(
    std::uint32_t tab_id, std::uint64_t navigation_id,
    const network::NetworkObservation& observation) {
  GatewayEvent event;
  event.source = EventSource::kNetwork;
  event.tab_id = tab_id;
  event.navigation_id = navigation_id;
  event.network = observation;
  return Submit(event);
}

std::vector<GatewayEvent> ObservationGateway::Drain(std::size_t max_events) {
  std::vector<GatewayEvent> drained;
  if (max_events == 0 || queue_.empty()) {
    return drained;
  }
  const std::size_t count = std::min(max_events, queue_.size());
  drained.assign(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(count));
  queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(count));
  return drained;
}

GatewayStats ObservationGateway::stats() const {
  GatewayStats stats;
  stats.queued = queue_.size();
  stats.dropped_stale_total = dropped_stale_total_;
  stats.dropped_backpressure_total = dropped_backpressure_total_;
  return stats;
}

}  // namespace crayon::cef_shell::gateway
