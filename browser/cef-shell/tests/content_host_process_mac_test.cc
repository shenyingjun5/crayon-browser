#include "macos/content_host_process_mac.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <variant>

namespace {

using crayon::browser::cef_shell::macos::ContentHostProcess;
namespace host = crayon::browser::cef_shell::macos::content_host_ipc;

bool WaitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

std::set<std::string> OwnedDirectories() {
  std::set<std::string> result;
  for (const auto& entry : std::filesystem::directory_iterator("/tmp")) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("crayon-content-", 0) == 0)
      result.insert(entry.path().string());
  }
  return result;
}

bool Run() {
  const auto before = OwnedDirectories();
  ContentHostProcess process;
  if (!process.Start(CRAYON_CONTENT_HOST_TEST_PATH) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(6))) {
    return false;
  }
  if (!process.Enqueue(host::Begin{"process", "tab-1", 1, 1,
                                   host::Mode::kStandard,
                                   "https://example.test/", "Example"})) {
    return false;
  }
  std::vector<host::Fact> facts;
  for (std::size_t index = 0; index < 64; ++index) {
    host::Fact fact;
    fact.kind = host::FactKind::kParagraph;
    fact.text.assign(500, static_cast<char>('a' + index % 20));
    facts.push_back(std::move(fact));
  }
  if (!process.Enqueue(
          host::FactBatch{"process", "tab-1", 1, 1, 0, std::move(facts)}) ||
      !process.Enqueue(host::Terminal{"process", "tab-1", 1, 1,
                                      host::TerminalStatus::kCompleted,
                                      host::EngineError::kNone})) {
    return false;
  }
  std::vector<host::Message> replies;
  if (!WaitFor(
          [&] {
            auto next = process.Drain(8);
            replies.insert(replies.end(), std::make_move_iterator(next.begin()),
                           std::make_move_iterator(next.end()));
            return !replies.empty();
          },
          std::chrono::seconds(6)) ||
      !std::holds_alternative<host::MarkdownChunk>(replies.front())) {
    return false;
  }

  if (!process.Enqueue(host::Shutdown{}) ||
      !WaitFor([&process] { return !process.healthy(); },
               std::chrono::seconds(3)) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(6))) {
    return false;
  }
  process.Stop();
  return !process.healthy() && OwnedDirectories() == before;
}

}  // namespace

int main() { return Run() ? 0 : 1; }
