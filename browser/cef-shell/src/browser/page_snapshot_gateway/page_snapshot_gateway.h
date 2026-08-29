#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <variant>
#include <vector>

#include "crayon/browser_engine/snapshot.h"

namespace crayon::cef_shell::gateway {

inline constexpr std::size_t kMaxQueuedSnapshotEvents = 16;
inline constexpr std::size_t kMaxRetiredSnapshotRequests = 128;

enum class IpcSourceKind { kRenderer = 0, kPage, kBrowser, kOther };

struct RendererSource final {
  IpcSourceKind kind = IpcSourceKind::kOther;
  std::uint64_t process_id = 0;
  std::uint64_t frame_id = 0;
  bool is_main_frame = false;
};

enum class SnapshotGatewayResult {
  kAccepted = 0,
  kIdempotent,
  kRejectedNotFound,
  kRejectedDuplicate,
  kRejectedInvalid,
  kRejectedSource,
  kRejectedStaleNavigation,
  kRejectedSequence,
  kRejectedBackpressure,
  kRejectedCapacity,
};

using SnapshotGatewayEvent = std::variant<browser_engine::SnapshotChunk,
                                          browser_engine::SnapshotTerminal>;

struct SnapshotGatewayStats final {
  std::size_t active_requests = 0;
  std::size_t queued_events = 0;
  std::uint64_t rejected_source_total = 0;
  std::uint64_t rejected_stale_total = 0;
  std::uint64_t rejected_sequence_total = 0;
  std::uint64_t dropped_backpressure_total = 0;
};

// Browser-process, single-threaded validation gate. BeginRequest is called
// only from trusted Browser code; renderer/page IPC can only submit against
// the exact identity bound there.
class PageSnapshotGateway final {
 public:
  SnapshotGatewayResult BeginRequest(
      const browser_engine::SnapshotRequest& request,
      const RendererSource& expected_renderer,
      const browser_engine::BrowserUrl& expected_document_url);
  SnapshotGatewayResult SubmitChunk(const RendererSource& source,
                                    browser_engine::SnapshotChunk chunk);
  SnapshotGatewayResult SubmitTerminal(
      const RendererSource& source, browser_engine::SnapshotTerminal terminal);
  SnapshotGatewayResult Cancel(
      const browser_engine::SnapshotRequestId& request_id);
  std::size_t AdvanceNavigation(const browser_engine::TabId& tab_id,
                                browser_engine::NavigationId navigation_id);
  std::size_t CloseTab(const browser_engine::TabId& tab_id);
  void ShutDown();

  std::vector<SnapshotGatewayEvent> Drain(std::size_t max_events);
  SnapshotGatewayStats stats() const noexcept;

 private:
  struct ActiveRequest final {
    browser_engine::SnapshotRequest request;
    RendererSource expected_renderer;
    browser_engine::BrowserUrl expected_document_url;
    std::uint32_t next_sequence = 0;
    std::size_t fact_count = 0;
    std::size_t byte_count = 0;
  };

  static bool SameSource(const RendererSource& left,
                         const RendererSource& right) noexcept;
  static bool ValidTerminal(
      const browser_engine::SnapshotTerminal& terminal) noexcept;
  bool Matches(const ActiveRequest& active,
               const browser_engine::SnapshotRequestId& request_id,
               const browser_engine::TabId& tab_id,
               browser_engine::NavigationId navigation_id) const noexcept;
  bool IsRetired(
      const browser_engine::SnapshotRequestId& request_id) const noexcept;
  void Retire(const browser_engine::SnapshotRequestId& request_id);
  void Complete(std::map<browser_engine::SnapshotRequestId,
                         ActiveRequest>::iterator active,
                browser_engine::SnapshotTerminalStatus status,
                browser_engine::EngineErrorCode error);
  bool shut_down_ = false;
  std::map<browser_engine::SnapshotRequestId, ActiveRequest> active_;
  std::vector<browser_engine::SnapshotRequestId> retired_;
  std::vector<SnapshotGatewayEvent> queue_;
  std::uint64_t rejected_source_total_ = 0;
  std::uint64_t rejected_stale_total_ = 0;
  std::uint64_t rejected_sequence_total_ = 0;
  std::uint64_t dropped_backpressure_total_ = 0;
};

}  // namespace crayon::cef_shell::gateway
