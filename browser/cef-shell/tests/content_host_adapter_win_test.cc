#include "windows/content_host_adapter_win.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using crayon::browser::cef_shell::windows::ContentHostAdapter;
using crayon::browser::cef_shell::windows::ContentHostTransport;
namespace host = crayon::browser::cef_shell::windows::content_host_ipc;
namespace engine = crayon::browser_engine;

class FakeTransport final : public ContentHostTransport {
 public:
  bool Start(std::string) override { return healthy_ = true; }
  void Stop() override { healthy_ = false; }
  bool Enqueue(host::Message message) override {
    if (!healthy_) return false;
    sent.push_back(std::move(message));
    return true;
  }
  std::vector<host::Message> Drain(std::size_t maximum) override {
    const std::size_t count = std::min(maximum, inbound.size());
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
  std::vector<host::Message> sent;
  std::vector<host::Message> inbound;
};

template <typename Id>
Id MakeId(const char *value) {
  return *Id::TryCreate(value);
}

bool Run() {
  auto transport = std::make_unique<FakeTransport>();
  FakeTransport *fake = transport.get();
  ContentHostAdapter adapter(std::move(transport));
  if (!adapter.Start("C:\\content-host.exe")) return false;
  const auto request = MakeId<engine::SnapshotRequestId>("request-1");
  const auto tab = MakeId<engine::TabId>("tab-1");
  const auto navigation = engine::NavigationId::FromRaw(7);
  const auto url = engine::BrowserUrl::TryParse("https://example.test/");
  adapter.OnSnapshotStarted(engine::SnapshotRequest{
      request, tab, navigation, engine::SnapshotMode::kStandard});
  engine::SnapshotFact paragraph;
  paragraph.kind = engine::SnapshotFactKind::kParagraph;
  paragraph.text = "Windows body";
  adapter.Consume(
      {engine::SnapshotChunk{request,
                             tab,
                             navigation,
                             0,
                             engine::SnapshotDocumentMetadata{*url, "Example"},
                             {paragraph}},
       engine::SnapshotTerminal{request, tab, navigation,
                                engine::SnapshotTerminalStatus::kCompleted,
                                engine::EngineErrorCode::kNone}});
  if (fake->sent.size() != 3 ||
      !std::holds_alternative<host::Begin>(fake->sent[0]) ||
      !std::holds_alternative<host::FactBatch>(fake->sent[1]) ||
      !std::holds_alternative<host::Terminal>(fake->sent[2])) {
    return false;
  }
  fake->inbound.push_back(
      host::MarkdownChunk{request.value(), tab.value(), navigation.value(),
                          navigation.value(), 0, true, "Windows body\n"});
  adapter.Tick();
  const auto replies = adapter.Drain(4);
  adapter.Stop();
  return replies.size() == 1 && !adapter.healthy();
}

}  // namespace

int main() { return Run() ? 0 : 1; }
