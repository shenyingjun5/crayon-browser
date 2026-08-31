#include "windows/media_host_process_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <thread>
#include <variant>
#include <vector>

namespace {

using crayon::browser::cef_shell::windows::MediaHostProcess;
namespace mh = crayon::browser::cef_shell::windows::media_host_ipc;

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
  MediaHostProcess process;
  if (!process.Start(CRAYON_MEDIA_HOST_TEST_PATH) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(8))) {
    return false;
  }
  const std::uint64_t first_generation = process.generation();
  if (first_generation == 0) return false;
  sentinel_write.Reset();
  DWORD available = 0;
  SetLastError(ERROR_SUCCESS);
  if (PeekNamedPipe(sentinel_read.Get(), nullptr, 0, nullptr, &available,
                    nullptr) ||
      GetLastError() != ERROR_BROKEN_PIPE) {
    return false;
  }
  sentinel_read.Reset();
  if (!process.Enqueue(mh::Navigation{"nav-1", "tab-1", 7, 9}) ||
      !process.Enqueue(mh::IngestUrl{
          "ingest-1", "tab-1", 7, 9, 123, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}) ||
      !process.Enqueue(mh::ListDevices{"devices-1", std::nullopt, 0}) ||
      !process.Enqueue(mh::PollSessionEvents{"events-1"})) {
    return false;
  }
  std::vector<mh::Message> replies;
  if (!WaitFor(
          [&] {
            auto next = process.Drain(8);
            replies.insert(replies.end(), std::make_move_iterator(next.begin()),
                           std::make_move_iterator(next.end()));
            return replies.size() >= 4;
          },
          std::chrono::seconds(8))) {
    return false;
  }
  const bool complete =
      std::any_of(replies.begin(), replies.end(),
                  [](const auto& message) {
                    return std::holds_alternative<mh::Ack>(message);
                  }) &&
      std::any_of(replies.begin(), replies.end(),
                  [](const auto& message) {
                    return std::holds_alternative<mh::CandidateReply>(message);
                  }) &&
      std::any_of(replies.begin(), replies.end(),
                  [](const auto& message) {
                    return std::holds_alternative<mh::DevicePageReply>(message);
                  }) &&
      std::any_of(replies.begin(), replies.end(), [](const auto& message) {
        return std::holds_alternative<mh::SessionEventsReply>(message);
      });
  if (!complete || !process.Enqueue(mh::Shutdown{}) ||
      !WaitFor([&process] { return !process.healthy(); },
               std::chrono::seconds(3)) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(8)) ||
      process.generation() <= first_generation) {
    return false;
  }
  process.Stop();
  return !process.healthy();
}

}  // namespace

int main() { return Run() ? 0 : 1; }
