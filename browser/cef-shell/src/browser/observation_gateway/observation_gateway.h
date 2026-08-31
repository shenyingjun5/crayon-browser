// CEF-12: observation gateway merging media (CEF-09) and network
// (CEF-11) observations and forwarding them toward the core client
// with per-tab generation fencing (PL-001/PL-002):
//   - a navigation bumps the tab generation;
//   - events carrying an older generation are dropped at ingestion
//     (late renderer/network stragglers, BR-007 shape);
//   - the outbound queue is bounded with explicit dropped counters
//     (backpressure: the caller drains, nothing blocks).
//
// Thread contract: single-threaded (browser process).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "browser/network_observer/network_observer.h"
#include "renderer/media_observer/media_observer.h"

namespace crayon::cef_shell::gateway {

/// Maximum queued outbound events.
inline constexpr std::size_t kMaxQueuedEvents = 256;

/// Closed event sources.
enum class EventSource { kMedia = 0, kNetwork };

/// One merged outbound event.
struct GatewayEvent {
  EventSource source = EventSource::kMedia;
  std::uint32_t tab_id = 0;
  std::uint64_t navigation_id = 0;
  std::uint32_t generation = 0;
  renderer::MediaObservation media;
  network::NetworkObservation network;
  bool eme_encrypted = false;
};

/// Ingest outcome.
enum class GatewayResult {
  kAccepted = 0,
  kDroppedStaleGeneration,
  kDroppedBackpressure,
};

/// Bounded counters for diagnostics.
struct GatewayStats {
  std::size_t queued = 0;
  std::uint64_t dropped_stale_total = 0;
  std::uint64_t dropped_backpressure_total = 0;
};

/// Per-tab generation-fenced merge queue toward the core client.
class ObservationGateway final {
 public:
  /// Marks a navigation on `tab_id`: the tab generation advances, the
  /// tab's current navigation id is recorded for precheck and all
  /// still-queued events of older generations are dropped immediately
  /// (late results never flow out).
  std::size_t AdvanceGeneration(std::uint32_t tab_id, std::uint64_t navigation_id);

  /// Current generation of a tab (starts at 0, first navigation -> 1).
  std::uint32_t GenerationOf(std::uint32_t tab_id) const;

  /// Ingests a media observation for the tab's current generation.
  GatewayResult SubmitMedia(std::uint32_t tab_id, std::uint64_t navigation_id,
                            const renderer::MediaObservation& observation,
                            bool eme_encrypted = false);

  /// Ingests a network observation for the tab's current generation.
  GatewayResult SubmitNetwork(std::uint32_t tab_id, std::uint64_t navigation_id,
                              const network::NetworkObservation& observation);

  /// Drains up to `max_events` queued events (oldest first); the core
  /// client consumes these batches.
  std::vector<GatewayEvent> Drain(std::size_t max_events);

  GatewayStats stats() const;

 private:
  struct TabState {
    std::uint32_t generation = 0;
    std::uint64_t current_navigation_id = 0;
  };

  GatewayResult Submit(GatewayEvent event);

  std::vector<std::pair<std::uint32_t, TabState>> tabs_;
  std::vector<GatewayEvent> queue_;
  std::uint64_t dropped_stale_total_ = 0;
  std::uint64_t dropped_backpressure_total_ = 0;
};

}  // namespace crayon::cef_shell::gateway
