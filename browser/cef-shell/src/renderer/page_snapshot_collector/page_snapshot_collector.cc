#include "renderer/page_snapshot_collector/page_snapshot_collector.h"

#include <utility>

namespace crayon::cef_shell::renderer {

CollectResult PageSnapshotCollector::Start(
    const browser_engine::SnapshotRequest& request, std::string main_frame_id,
    browser_engine::SnapshotDocumentMetadata document) {
  const browser_engine::SnapshotChunk metadata_chunk{request.request_id,
                                                     request.tab_id,
                                                     request.navigation_id,
                                                     0,
                                                     document,
                                                     {}};
  if (torn_down_ || active_ || main_frame_id.empty() ||
      request.navigation_id.value() == 0 ||
      !browser_engine::IsValid(request.mode) ||
      !browser_engine::IsValid(metadata_chunk, request.mode)) {
    return CollectResult::kRejectedInactive;
  }
  request_ = request;
  document_ = std::move(document);
  main_frame_id_ = std::move(main_frame_id);
  next_sequence_ = 0;
  total_facts_ = 0;
  total_bytes_ = 0;
  pending_bytes_ = *browser_engine::SnapshotChunkByteSize(metadata_chunk);
  pending_.clear();
  active_ = true;
  return CollectResult::kAccepted;
}

CollectResult PageSnapshotCollector::Observe(RendererFact fact) {
  if (!active_ || torn_down_) {
    return CollectResult::kRejectedInactive;
  }
  if (fact.navigation_id != request_->navigation_id.value()) {
    return CollectResult::kDroppedStaleNavigation;
  }
  if (!fact.is_main_frame || fact.frame_id != main_frame_id_) {
    return CollectResult::kDroppedSubframe;
  }
  if (!fact.is_visible) {
    return CollectResult::kDroppedHidden;
  }
  if (!fact.is_same_origin) {
    return CollectResult::kDroppedCrossOrigin;
  }
  if (!browser_engine::IsValid(fact.fact, request_->mode)) {
    return CollectResult::kRejectedInvalidFact;
  }

  const auto fact_bytes = browser_engine::SnapshotFactByteSize(fact.fact);
  if (!fact_bytes.has_value() ||
      total_facts_ >= browser_engine::SnapshotModeMaxFacts(request_->mode) ||
      *fact_bytes >
          browser_engine::SnapshotModeMaxBytes(request_->mode) - total_bytes_) {
    pending_.clear();
    EmitTerminal(browser_engine::SnapshotTerminalStatus::kRejected,
                 browser_engine::EngineErrorCode::kCapacityExceeded);
    return CollectResult::kRejectedCapacity;
  }

  if (pending_.size() >= browser_engine::kMaxSnapshotFactsPerChunk ||
      *fact_bytes > browser_engine::kMaxSnapshotChunkBytes - pending_bytes_) {
    if (const auto result = Flush(); result != CollectResult::kAccepted) {
      return result;
    }
  }
  pending_.push_back(std::move(fact.fact));
  ++total_facts_;
  total_bytes_ += *fact_bytes;
  pending_bytes_ += *fact_bytes;
  return CollectResult::kAccepted;
}

CollectResult PageSnapshotCollector::Finish() {
  if (!active_ || torn_down_) {
    return CollectResult::kRejectedInactive;
  }
  if (!pending_.empty() || next_sequence_ == 0) {
    if (const auto result = Flush(); result != CollectResult::kAccepted) {
      return result;
    }
  }
  EmitTerminal(browser_engine::SnapshotTerminalStatus::kCompleted,
               browser_engine::EngineErrorCode::kNone);
  return CollectResult::kAccepted;
}

void PageSnapshotCollector::RejectCapacity() {
  if (!active_ || torn_down_) return;
  pending_.clear();
  EmitTerminal(browser_engine::SnapshotTerminalStatus::kRejected,
               browser_engine::EngineErrorCode::kCapacityExceeded);
}

void PageSnapshotCollector::Cancel() {
  if (!active_ || torn_down_) {
    return;
  }
  pending_.clear();
  EmitTerminal(browser_engine::SnapshotTerminalStatus::kCancelled,
               browser_engine::EngineErrorCode::kNone);
}

void PageSnapshotCollector::TearDown() {
  pending_.clear();
  active_ = false;
  torn_down_ = true;
}

CollectResult PageSnapshotCollector::Flush() {
  if (next_sequence_ >= browser_engine::kMaxSnapshotChunks) {
    pending_.clear();
    EmitTerminal(browser_engine::SnapshotTerminalStatus::kRejected,
                 browser_engine::EngineErrorCode::kCapacityExceeded);
    return CollectResult::kRejectedCapacity;
  }
  browser_engine::SnapshotChunk chunk{
      request_->request_id,
      request_->tab_id,
      request_->navigation_id,
      next_sequence_,
      next_sequence_ == 0 ? document_ : std::nullopt,
      std::move(pending_)};
  pending_.clear();
  pending_bytes_ = 0;
  ++next_sequence_;
  sink_.OnRendererSnapshotChunk(chunk);
  return CollectResult::kAccepted;
}

void PageSnapshotCollector::EmitTerminal(
    browser_engine::SnapshotTerminalStatus status,
    browser_engine::EngineErrorCode error) {
  active_ = false;
  sink_.OnRendererSnapshotTerminal(
      browser_engine::SnapshotTerminal{request_->request_id, request_->tab_id,
                                       request_->navigation_id, status, error});
}

}  // namespace crayon::cef_shell::renderer
