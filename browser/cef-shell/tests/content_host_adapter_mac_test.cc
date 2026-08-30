#include "macos/content_host_adapter_mac.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using crayon::browser::cef_shell::macos::ContentHostAdapter;
using crayon::browser::cef_shell::macos::ContentHostTransport;
namespace host = crayon::browser::cef_shell::macos::content_host_ipc;
namespace engine = crayon::browser_engine;
namespace gateway = crayon::browser::cef_shell::gateway;

class FakeTransport final : public ContentHostTransport {
 public:
  bool Start(std::string) override {
    healthy_ = true;
    return true;
  }
  void Stop() override { healthy_ = false; }
  bool Enqueue(host::Message message) override {
    if (!healthy_ || !accept_) return false;
    sent.push_back(std::move(message));
    return true;
  }
  std::vector<host::Message> Drain(std::size_t max_messages) override {
    const std::size_t count = std::min(max_messages, inbound.size());
    std::vector<host::Message> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(inbound.front()));
      inbound.erase(inbound.begin());
    }
    return result;
  }
  bool healthy() const noexcept override { return healthy_; }

  bool healthy_ = false;
  bool accept_ = true;
  std::vector<host::Message> sent;
  std::vector<host::Message> inbound;
};

template <typename Id>
Id MakeId(const char* value) {
  return *Id::TryCreate(value);
}

bool Run() {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport* fake = transport.get();
  ContentHostAdapter adapter(std::move(transport));
  if (!adapter.Start("/test/content-host")) return false;

  const auto request_id = MakeId<engine::SnapshotRequestId>("request-1");
  const auto tab_id = MakeId<engine::TabId>("tab-1");
  const auto navigation = engine::NavigationId::FromRaw(7);
  adapter.OnSnapshotStarted(engine::SnapshotRequest{
      request_id, tab_id, navigation, engine::SnapshotMode::kStandard});
  const auto url = engine::BrowserUrl::TryParse("https://example.test/");
  adapter.Consume(
      {engine::SnapshotChunk{request_id,
                             tab_id,
                             navigation,
                             0,
                             engine::SnapshotDocumentMetadata{*url, "Example"},
                             {}}});
  engine::SnapshotFact paragraph;
  paragraph.kind = engine::SnapshotFactKind::kParagraph;
  paragraph.text = "Body";
  adapter.Consume(
      {engine::SnapshotChunk{
           request_id, tab_id, navigation, 1, std::nullopt, {paragraph}},
       engine::SnapshotTerminal{request_id, tab_id, navigation,
                                engine::SnapshotTerminalStatus::kCompleted,
                                engine::EngineErrorCode::kNone}});
  if (fake->sent.size() != 3 ||
      !std::holds_alternative<host::Begin>(fake->sent[0]) ||
      !std::holds_alternative<host::FactBatch>(fake->sent[1]) ||
      std::get<host::FactBatch>(fake->sent[1]).sequence != 0 ||
      !std::holds_alternative<host::Terminal>(fake->sent[2])) {
    return false;
  }

  fake->inbound.push_back(host::MarkdownChunk{
      request_id.value(), tab_id.value(), navigation.value(),
      navigation.value(), 0, true, "Body\n"});
  adapter.Tick();
  const auto replies = adapter.Drain(4);
  if (replies.size() != 1 ||
      !std::holds_alternative<host::MarkdownChunk>(replies.front())) {
    return false;
  }

  const auto stale_id = MakeId<engine::SnapshotRequestId>("request-2");
  adapter.OnSnapshotStarted(engine::SnapshotRequest{
      stale_id, tab_id, navigation, engine::SnapshotMode::kCompact});
  adapter.OnSnapshotNavigation(tab_id, engine::NavigationId::FromRaw(8));
  fake->inbound.push_back(
      host::MarkdownChunk{stale_id.value(), tab_id.value(), navigation.value(),
                          navigation.value(), 0, true, "stale"});
  adapter.Tick();
  if (!adapter.Drain(4).empty() ||
      !std::holds_alternative<host::Navigation>(fake->sent.back())) {
    return false;
  }

  const auto cancel_id = MakeId<engine::SnapshotRequestId>("request-3");
  adapter.OnSnapshotStarted(engine::SnapshotRequest{
      cancel_id, tab_id, engine::NavigationId::FromRaw(8),
      engine::SnapshotMode::kStandard});
  adapter.OnSnapshotCancelled(cancel_id);
  if (!std::holds_alternative<host::Cancel>(fake->sent.back())) return false;
  adapter.OnSnapshotClosed(tab_id);
  if (!std::holds_alternative<host::CloseTab>(fake->sent.back())) return false;

  const auto rejected_id = MakeId<engine::SnapshotRequestId>("request-4");
  adapter.OnSnapshotStarted(engine::SnapshotRequest{
      rejected_id, tab_id, engine::NavigationId::FromRaw(9),
      engine::SnapshotMode::kStandard});
  fake->accept_ = false;
  adapter.Consume(
      {engine::SnapshotChunk{rejected_id,
                             tab_id,
                             engine::NavigationId::FromRaw(9),
                             0,
                             engine::SnapshotDocumentMetadata{*url, "Rejected"},
                             {}}});
  fake->accept_ = true;
  fake->inbound.push_back(host::MarkdownChunk{
      rejected_id.value(), tab_id.value(), 9, 9, 0, true, "rejected"});
  adapter.Tick();
  if (!adapter.Drain(4).empty()) return false;
  adapter.Stop();
  return !adapter.healthy();
}

}  // namespace

int main() { return Run() ? 0 : 1; }
