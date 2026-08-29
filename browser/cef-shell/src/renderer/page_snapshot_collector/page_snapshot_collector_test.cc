#include "renderer/page_snapshot_collector/page_snapshot_collector.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using crayon::browser_engine::NavigationId;
using crayon::browser_engine::SnapshotChunk;
using crayon::browser_engine::SnapshotDocumentMetadata;
using crayon::browser_engine::SnapshotFact;
using crayon::browser_engine::SnapshotFactKind;
using crayon::browser_engine::SnapshotMode;
using crayon::browser_engine::SnapshotRequest;
using crayon::browser_engine::SnapshotRequestId;
using crayon::browser_engine::SnapshotTerminal;
using crayon::browser_engine::SnapshotTerminalStatus;
using crayon::browser_engine::TabId;
using crayon::cef_shell::renderer::CollectResult;
using crayon::cef_shell::renderer::PageSnapshotCollector;
using crayon::cef_shell::renderer::PageSnapshotCollectorSink;
using crayon::cef_shell::renderer::RendererFact;

void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Id>
Id MakeId(const char* value) {
  auto id = Id::TryCreate(value);
  Check(id.has_value(), "test ID must be valid");
  return std::move(*id);
}

class RecordingSink final : public PageSnapshotCollectorSink {
 public:
  void OnRendererSnapshotChunk(const SnapshotChunk& chunk) override {
    chunks.push_back(chunk);
  }
  void OnRendererSnapshotTerminal(const SnapshotTerminal& terminal) override {
    terminals.push_back(terminal);
  }

  std::vector<SnapshotChunk> chunks;
  std::vector<SnapshotTerminal> terminals;
};

SnapshotRequest MakeRequest(const char* id = "snapshot-renderer") {
  return SnapshotRequest{MakeId<SnapshotRequestId>(id),
                         MakeId<TabId>("tab-renderer"),
                         NavigationId::FromRaw(41), SnapshotMode::kStandard};
}

SnapshotDocumentMetadata MakeDocument() {
  auto url = crayon::browser_engine::BrowserUrl::TryParse(
      "https://example.test/article");
  Check(url.has_value(), "document URL must be valid");
  return SnapshotDocumentMetadata{std::move(*url), "Article title"};
}

RendererFact MakeParagraph(std::string text) {
  SnapshotFact fact;
  fact.kind = SnapshotFactKind::kParagraph;
  fact.text = std::move(text);
  return RendererFact{std::move(fact), 41, 7, true, true, true};
}

void TestVisibilityAndSourceFilters() {
  RecordingSink sink;
  PageSnapshotCollector collector(sink);
  Check(collector.Start(MakeRequest(), 7, MakeDocument()) ==
            CollectResult::kAccepted,
        "collector must start");
  auto hidden = MakeParagraph("hidden");
  hidden.is_visible = false;
  Check(collector.Observe(std::move(hidden)) == CollectResult::kDroppedHidden,
        "hidden content must drop");
  auto cross_origin = MakeParagraph("cross-origin");
  cross_origin.is_same_origin = false;
  Check(collector.Observe(std::move(cross_origin)) ==
            CollectResult::kDroppedCrossOrigin,
        "cross-origin content must drop");
  auto subframe = MakeParagraph("subframe");
  subframe.is_main_frame = false;
  Check(
      collector.Observe(std::move(subframe)) == CollectResult::kDroppedSubframe,
      "subframe content must drop");
  auto stale = MakeParagraph("stale");
  stale.navigation_id = 40;
  Check(collector.Observe(std::move(stale)) ==
            CollectResult::kDroppedStaleNavigation,
        "stale navigation content must drop");
  Check(collector.pending_fact_count() == 0,
        "filtered facts must not enter buffer");
}

void TestBoundedChunkingAndFinish() {
  RecordingSink sink;
  PageSnapshotCollector collector(sink);
  Check(collector.Start(MakeRequest("snapshot-chunks"), 7, MakeDocument()) ==
            CollectResult::kAccepted,
        "chunk collector must start");
  for (std::size_t index = 0;
       index < crayon::browser_engine::kMaxSnapshotFactsPerChunk + 1; ++index) {
    Check(collector.Observe(
              MakeParagraph("paragraph-" + std::to_string(index))) ==
              CollectResult::kAccepted,
          "visible fact must collect");
  }
  Check(sink.chunks.size() == 1 && sink.chunks.front().facts.size() == 64,
        "collector must flush at fact bound");
  Check(collector.Finish() == CollectResult::kAccepted, "finish must pass");
  Check(sink.chunks.size() == 2 && sink.chunks[1].sequence == 1 &&
            sink.chunks[1].facts.size() == 1,
        "finish must flush remainder in order");
  Check(sink.terminals.size() == 1 &&
            sink.terminals.front().status == SnapshotTerminalStatus::kCompleted,
        "finish must emit one terminal");
  Check(collector.Observe(MakeParagraph("late")) ==
            CollectResult::kRejectedInactive,
        "late fact must fail");
}

void TestCancelAndTeardownFence() {
  RecordingSink sink;
  PageSnapshotCollector collector(sink);
  Check(collector.Start(MakeRequest("snapshot-cancel-renderer"), 7,
                        MakeDocument()) == CollectResult::kAccepted,
        "cancel collector must start");
  Check(collector.Observe(MakeParagraph("pending")) == CollectResult::kAccepted,
        "pending fact must collect");
  collector.Cancel();
  collector.Cancel();
  Check(sink.chunks.empty(), "cancel must discard partial chunk");
  Check(sink.terminals.size() == 1 &&
            sink.terminals.front().status == SnapshotTerminalStatus::kCancelled,
        "cancel must emit exactly one terminal");

  RecordingSink teardown_sink;
  PageSnapshotCollector teardown(teardown_sink);
  Check(teardown.Start(MakeRequest("snapshot-teardown"), 7, MakeDocument()) ==
            CollectResult::kAccepted,
        "teardown collector must start");
  teardown.TearDown();
  Check(teardown.Observe(MakeParagraph("late")) ==
            CollectResult::kRejectedInactive,
        "teardown must fence late fact");
  Check(teardown_sink.chunks.empty() && teardown_sink.terminals.empty(),
        "teardown must suppress callbacks");
}

}  // namespace

int main() {
  try {
    TestVisibilityAndSourceFilters();
    TestBoundedChunkingAndFinish();
    TestCancelAndTeardownFence();
    std::cout << "page_snapshot_collector_test: passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "page_snapshot_collector_test: " << error.what() << '\n';
    return 1;
  }
}
