#include "browser/page_snapshot_gateway/page_snapshot_gateway.h"

#include <algorithm>
#include <utility>

namespace crayon::cef_shell::gateway {

SnapshotGatewayResult PageSnapshotGateway::BeginRequest(
    const browser_engine::SnapshotRequest& request,
    const RendererSource& expected_renderer,
    const browser_engine::BrowserUrl& expected_document_url) {
  if (shut_down_ || request.navigation_id.value() == 0 ||
      !browser_engine::IsValid(request.mode) ||
      expected_renderer.kind != IpcSourceKind::kRenderer ||
      expected_renderer.process_id == 0 || expected_renderer.frame_id.empty() ||
      !expected_renderer.is_main_frame) {
    return SnapshotGatewayResult::kRejectedInvalid;
  }
  if (active_.count(request.request_id) != 0 || IsRetired(request.request_id)) {
    return SnapshotGatewayResult::kRejectedDuplicate;
  }
  if (active_.size() >= browser_engine::kMaxSnapshotStreams ||
      active_.size() + queue_.size() >= kMaxQueuedSnapshotEvents) {
    return SnapshotGatewayResult::kRejectedCapacity;
  }
  active_.emplace(request.request_id,
                  ActiveRequest{request, expected_renderer,
                                expected_document_url, 0, 0, 0});
  return SnapshotGatewayResult::kAccepted;
}

SnapshotGatewayResult PageSnapshotGateway::SubmitChunk(
    const RendererSource& source, browser_engine::SnapshotChunk chunk) {
  const auto active = active_.find(chunk.request_id);
  if (active == active_.end()) {
    return SnapshotGatewayResult::kRejectedNotFound;
  }
  if (!SameSource(source, active->second.expected_renderer)) {
    ++rejected_source_total_;
    return SnapshotGatewayResult::kRejectedSource;
  }
  if (!Matches(active->second, chunk.request_id, chunk.tab_id,
               chunk.navigation_id)) {
    ++rejected_stale_total_;
    return SnapshotGatewayResult::kRejectedStaleNavigation;
  }
  if (chunk.sequence != active->second.next_sequence ||
      chunk.sequence >= browser_engine::kMaxSnapshotChunks) {
    ++rejected_sequence_total_;
    return SnapshotGatewayResult::kRejectedSequence;
  }
  if (!browser_engine::IsValid(chunk, active->second.request.mode)) {
    return SnapshotGatewayResult::kRejectedInvalid;
  }
  if (chunk.sequence == 0 &&
      !(chunk.document->url == active->second.expected_document_url)) {
    ++rejected_stale_total_;
    return SnapshotGatewayResult::kRejectedStaleNavigation;
  }
  std::size_t chunk_fact_bytes = 0;
  for (const auto& fact : chunk.facts) {
    const auto fact_bytes = browser_engine::SnapshotFactByteSize(fact);
    if (!fact_bytes.has_value()) {
      return SnapshotGatewayResult::kRejectedInvalid;
    }
    chunk_fact_bytes += *fact_bytes;
  }
  if (chunk.facts.size() >
          browser_engine::SnapshotModeMaxFacts(active->second.request.mode) -
              active->second.fact_count ||
      chunk_fact_bytes >
          browser_engine::SnapshotModeMaxBytes(active->second.request.mode) -
              active->second.byte_count) {
    return SnapshotGatewayResult::kRejectedCapacity;
  }
  if (queue_.size() + active_.size() >= kMaxQueuedSnapshotEvents) {
    ++dropped_backpressure_total_;
    return SnapshotGatewayResult::kRejectedBackpressure;
  }
  ++active->second.next_sequence;
  active->second.fact_count += chunk.facts.size();
  active->second.byte_count += chunk_fact_bytes;
  queue_.emplace_back(std::move(chunk));
  return SnapshotGatewayResult::kAccepted;
}

SnapshotGatewayResult PageSnapshotGateway::SubmitTerminal(
    const RendererSource& source, browser_engine::SnapshotTerminal terminal) {
  const auto active = active_.find(terminal.request_id);
  if (active == active_.end()) {
    return SnapshotGatewayResult::kRejectedNotFound;
  }
  if (!SameSource(source, active->second.expected_renderer)) {
    ++rejected_source_total_;
    return SnapshotGatewayResult::kRejectedSource;
  }
  if (!Matches(active->second, terminal.request_id, terminal.tab_id,
               terminal.navigation_id)) {
    ++rejected_stale_total_;
    return SnapshotGatewayResult::kRejectedStaleNavigation;
  }
  if (!ValidTerminal(terminal)) {
    return SnapshotGatewayResult::kRejectedInvalid;
  }
  if (terminal.status == browser_engine::SnapshotTerminalStatus::kCompleted &&
      active->second.next_sequence == 0) {
    return SnapshotGatewayResult::kRejectedInvalid;
  }
  if (terminal.status != browser_engine::SnapshotTerminalStatus::kCompleted) {
    const auto request_id = terminal.request_id;
    queue_.erase(
        std::remove_if(queue_.begin(), queue_.end(),
                       [&request_id](const SnapshotGatewayEvent& event) {
                         const auto* chunk =
                             std::get_if<browser_engine::SnapshotChunk>(&event);
                         return chunk && chunk->request_id == request_id;
                       }),
        queue_.end());
  }
  Retire(terminal.request_id);
  active_.erase(active);
  queue_.emplace_back(std::move(terminal));
  return SnapshotGatewayResult::kAccepted;
}

SnapshotGatewayResult PageSnapshotGateway::Cancel(
    const browser_engine::SnapshotRequestId& request_id) {
  if (IsRetired(request_id)) {
    return SnapshotGatewayResult::kIdempotent;
  }
  const auto active = active_.find(request_id);
  if (active == active_.end()) {
    return SnapshotGatewayResult::kRejectedNotFound;
  }
  Complete(active, browser_engine::SnapshotTerminalStatus::kCancelled,
           browser_engine::EngineErrorCode::kNone);
  return SnapshotGatewayResult::kAccepted;
}

SnapshotGatewayResult PageSnapshotGateway::Reject(
    const browser_engine::SnapshotRequestId& request_id,
    browser_engine::EngineErrorCode error) {
  if (error == browser_engine::EngineErrorCode::kNone) {
    return SnapshotGatewayResult::kRejectedInvalid;
  }
  if (IsRetired(request_id)) return SnapshotGatewayResult::kIdempotent;
  const auto active = active_.find(request_id);
  if (active == active_.end()) {
    return SnapshotGatewayResult::kRejectedNotFound;
  }
  Complete(active, browser_engine::SnapshotTerminalStatus::kRejected, error);
  return SnapshotGatewayResult::kAccepted;
}

std::size_t PageSnapshotGateway::AdvanceNavigation(
    const browser_engine::TabId& tab_id,
    browser_engine::NavigationId navigation_id) {
  std::vector<browser_engine::SnapshotRequestId> stale;
  for (const auto& entry : active_) {
    if (entry.second.request.tab_id == tab_id &&
        entry.second.request.navigation_id != navigation_id) {
      stale.push_back(entry.first);
    }
  }
  for (const auto& request_id : stale) {
    Complete(active_.find(request_id),
             browser_engine::SnapshotTerminalStatus::kStaleNavigation,
             browser_engine::EngineErrorCode::kStaleNavigation);
  }
  rejected_stale_total_ += stale.size();
  return stale.size();
}

std::size_t PageSnapshotGateway::CloseTab(const browser_engine::TabId& tab_id) {
  std::vector<browser_engine::SnapshotRequestId> closing;
  for (const auto& entry : active_) {
    if (entry.second.request.tab_id == tab_id) {
      closing.push_back(entry.first);
    }
  }
  for (const auto& request_id : closing) {
    Complete(active_.find(request_id),
             browser_engine::SnapshotTerminalStatus::kCancelled,
             browser_engine::EngineErrorCode::kNone);
  }
  return closing.size();
}

std::size_t PageSnapshotGateway::FailTab(
    const browser_engine::TabId& tab_id,
    browser_engine::EngineErrorCode error) {
  if (error == browser_engine::EngineErrorCode::kNone) return 0;
  std::vector<browser_engine::SnapshotRequestId> failed;
  for (const auto& entry : active_) {
    if (entry.second.request.tab_id == tab_id) {
      failed.push_back(entry.first);
    }
  }
  for (const auto& request_id : failed) {
    Complete(active_.find(request_id),
             browser_engine::SnapshotTerminalStatus::kRejected, error);
  }
  return failed.size();
}

void PageSnapshotGateway::ShutDown() {
  shut_down_ = true;
  active_.clear();
  queue_.clear();
}

std::vector<SnapshotGatewayEvent> PageSnapshotGateway::Drain(
    std::size_t max_events) {
  const auto count = std::min(max_events, queue_.size());
  std::vector<SnapshotGatewayEvent> result;
  result.reserve(count);
  std::move(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(count),
            std::back_inserter(result));
  queue_.erase(queue_.begin(),
               queue_.begin() + static_cast<std::ptrdiff_t>(count));
  return result;
}

SnapshotGatewayStats PageSnapshotGateway::stats() const noexcept {
  return SnapshotGatewayStats{
      active_.size(),           queue_.size(),
      rejected_source_total_,   rejected_stale_total_,
      rejected_sequence_total_, dropped_backpressure_total_};
}

bool PageSnapshotGateway::SameSource(const RendererSource& left,
                                     const RendererSource& right) noexcept {
  return left.kind == IpcSourceKind::kRenderer && left.kind == right.kind &&
         left.process_id == right.process_id &&
         left.frame_id == right.frame_id && left.is_main_frame &&
         right.is_main_frame;
}

bool PageSnapshotGateway::ValidTerminal(
    const browser_engine::SnapshotTerminal& terminal) noexcept {
  if (!browser_engine::IsValid(terminal.status)) {
    return false;
  }
  if (terminal.status == browser_engine::SnapshotTerminalStatus::kCompleted ||
      terminal.status == browser_engine::SnapshotTerminalStatus::kCancelled) {
    return terminal.error == browser_engine::EngineErrorCode::kNone;
  }
  if (terminal.status ==
      browser_engine::SnapshotTerminalStatus::kStaleNavigation) {
    return terminal.error == browser_engine::EngineErrorCode::kStaleNavigation;
  }
  return terminal.error != browser_engine::EngineErrorCode::kNone;
}

bool PageSnapshotGateway::Matches(
    const ActiveRequest& active,
    const browser_engine::SnapshotRequestId& request_id,
    const browser_engine::TabId& tab_id,
    browser_engine::NavigationId navigation_id) const noexcept {
  return active.request.request_id == request_id &&
         active.request.tab_id == tab_id &&
         active.request.navigation_id == navigation_id;
}

bool PageSnapshotGateway::IsRetired(
    const browser_engine::SnapshotRequestId& request_id) const noexcept {
  return std::find(retired_.begin(), retired_.end(), request_id) !=
         retired_.end();
}

void PageSnapshotGateway::Retire(
    const browser_engine::SnapshotRequestId& request_id) {
  if (retired_.size() >= kMaxRetiredSnapshotRequests) {
    retired_.erase(retired_.begin());
  }
  retired_.push_back(request_id);
}

void PageSnapshotGateway::Complete(
    std::map<browser_engine::SnapshotRequestId, ActiveRequest>::iterator active,
    browser_engine::SnapshotTerminalStatus status,
    browser_engine::EngineErrorCode error) {
  const auto request = active->second.request;
  queue_.erase(
      std::remove_if(queue_.begin(), queue_.end(),
                     [&request](const SnapshotGatewayEvent& event) {
                       const auto* chunk =
                           std::get_if<browser_engine::SnapshotChunk>(&event);
                       return chunk != nullptr &&
                              chunk->request_id == request.request_id;
                     }),
      queue_.end());
  Retire(request.request_id);
  active_.erase(active);
  queue_.emplace_back(
      browser_engine::SnapshotTerminal{request.request_id, request.tab_id,
                                       request.navigation_id, status, error});
}

}  // namespace crayon::cef_shell::gateway
