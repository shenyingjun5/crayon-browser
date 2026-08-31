#include "windows/content_host_process_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using crayon::browser::cef_shell::windows::ContentHostProcess;
namespace host = crayon::browser::cef_shell::windows::content_host_ipc;

class ScopedHandle final {
 public:
  ~ScopedHandle() { Reset(); }

  HANDLE* Put() { return &handle_; }
  HANDLE Get() const { return handle_; }
  void Reset() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = nullptr;
  }

 private:
  HANDLE handle_ = nullptr;
};

bool WaitFor(const std::function<bool()>& predicate,
             std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

bool Run() {
  SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  ScopedHandle sentinel_read;
  ScopedHandle sentinel_write;
  if (!CreatePipe(sentinel_read.Put(), sentinel_write.Put(), &inheritable, 0)) {
    return false;
  }
  ContentHostProcess process;
  if (!process.Start(CRAYON_CONTENT_HOST_TEST_PATH) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(6))) {
    return false;
  }
  sentinel_write.Reset();
  DWORD available = 0;
  SetLastError(ERROR_SUCCESS);
  if (PeekNamedPipe(sentinel_read.Get(), nullptr, 0, nullptr, &available,
                    nullptr) ||
      GetLastError() != ERROR_BROKEN_PIPE) {
    return false;
  }
  sentinel_read.Reset();
  if (!process.Enqueue(host::Begin{"process", "tab-1", 1, 1,
                                   host::Mode::kStandard,
                                   "https://example.test/", "Example"}) ||
      !process.Enqueue(host::FactBatch{
          "process", "tab-1", 1, 1, 0,
          std::vector<host::Fact>{
              host::Fact{host::FactKind::kHeading,
                         "Heading",
                         std::nullopt,
                         std::nullopt,
                         1,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kParagraph,
                         "Windows body",
                         std::nullopt,
                         std::nullopt,
                         0,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kListItem,
                         "Item",
                         std::nullopt,
                         std::nullopt,
                         0,
                         1,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kLink,
                         "Link",
                         std::string("https://example.test/link"),
                         std::nullopt,
                         0,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kImage,
                         "Alt",
                         std::string("https://example.test/image.png"),
                         std::nullopt,
                         0,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kTable,
                         "",
                         std::nullopt,
                         std::nullopt,
                         0,
                         0,
                         false,
                         std::nullopt,
                         2,
                         {"A", "B", "1", "2"}},
              host::Fact{host::FactKind::kCodeBlock,
                         "let x = 1;\n",
                         std::nullopt,
                         std::string("cpp"),
                         0,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kDivider,
                         "",
                         std::nullopt,
                         std::nullopt,
                         0,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}},
              host::Fact{host::FactKind::kQuote,
                         "Quote",
                         std::nullopt,
                         std::nullopt,
                         0,
                         0,
                         false,
                         std::nullopt,
                         0,
                         {}}}}) ||
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
  return !process.healthy();
}

}  // namespace

int main() { return Run() ? 0 : 1; }
