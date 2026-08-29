#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "crayon/browser_engine/snapshot.h"

namespace crayon::cef_shell::renderer {

enum class CollectResult {
  kAccepted = 0,
  kDroppedHidden,
  kDroppedCrossOrigin,
  kDroppedSubframe,
  kDroppedStaleNavigation,
  kRejectedInvalidFact,
  kRejectedCapacity,
  kRejectedInactive,
};

struct RendererFact final {
  browser_engine::SnapshotFact fact;
  std::uint64_t navigation_id = 0;
  std::uint64_t frame_id = 0;
  bool is_main_frame = false;
  bool is_visible = false;
  bool is_same_origin = false;
};

class PageSnapshotCollectorSink {
 public:
  virtual ~PageSnapshotCollectorSink() = default;
  virtual void OnRendererSnapshotChunk(
      const browser_engine::SnapshotChunk& chunk) = 0;
  virtual void OnRendererSnapshotTerminal(
      const browser_engine::SnapshotTerminal& terminal) = 0;
};

// Single-threaded renderer-main collector. DOM traversal stays in the CEF
// adapter; this class admits only normalized, visible, same-origin main-frame
// facts and emits bounded chunks.
class PageSnapshotCollector final {
 public:
  explicit PageSnapshotCollector(PageSnapshotCollectorSink& sink)
      : sink_(sink) {}

  CollectResult Start(const browser_engine::SnapshotRequest& request,
                      std::uint64_t main_frame_id,
                      browser_engine::SnapshotDocumentMetadata document);
  CollectResult Observe(RendererFact fact);
  CollectResult Finish();
  void Cancel();
  void TearDown();

  bool active() const noexcept { return active_; }
  std::size_t pending_fact_count() const noexcept { return pending_.size(); }

 private:
  CollectResult Flush();
  void EmitTerminal(browser_engine::SnapshotTerminalStatus status,
                    browser_engine::EngineErrorCode error);

  PageSnapshotCollectorSink& sink_;
  bool active_ = false;
  bool torn_down_ = false;
  std::uint64_t main_frame_id_ = 0;
  std::uint32_t next_sequence_ = 0;
  std::size_t total_facts_ = 0;
  std::size_t total_bytes_ = 0;
  std::size_t pending_bytes_ = 0;
  std::optional<browser_engine::SnapshotRequest> request_;
  std::optional<browser_engine::SnapshotDocumentMetadata> document_;
  std::vector<browser_engine::SnapshotFact> pending_;
};

}  // namespace crayon::cef_shell::renderer
