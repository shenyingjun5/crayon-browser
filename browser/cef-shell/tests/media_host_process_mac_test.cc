#include "macos/media_host_process_mac.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>
#include <thread>
#include <variant>

namespace {

using crayon::browser::cef_shell::macos::MediaHostProcess;
namespace mh = crayon::browser::cef_shell::macos::media_host_ipc;

bool WaitFor(const std::function<bool()> &predicate,
             std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

std::set<std::string> OwnedDirectories() {
  std::set<std::string> result;
  for (const auto &entry : std::filesystem::directory_iterator("/tmp")) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("crayon-media-", 0) == 0)
      result.insert(entry.path().string());
  }
  return result;
}

bool Run() {
  const auto before = OwnedDirectories();
  MediaHostProcess process;
  if (!process.Start(CRAYON_MEDIA_HOST_TEST_PATH) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(6))) {
    std::cerr << "media-host process failed at startup\n";
    return false;
  }
  const std::uint64_t first_generation = process.generation();
  if (first_generation == 0)
    return false;
  if (!process.Enqueue(mh::Navigation{"nav-1", "tab-1", 7, 9}) ||
      !process.Enqueue(mh::IngestUrl{
          "ingest-1", "tab-1", 7, 9, 123, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}))
    return false;
  std::vector<mh::Message> replies;
  if (!WaitFor(
          [&] {
            auto next = process.Drain(8);
            replies.insert(replies.end(), std::make_move_iterator(next.begin()),
                           std::make_move_iterator(next.end()));
            return replies.size() >= 2;
          },
          std::chrono::seconds(6))) {
    std::cerr << "media-host process failed waiting for replies\n";
    return false;
  }
  const bool saw_ack =
      std::any_of(replies.begin(), replies.end(), [](const auto &m) {
        return std::holds_alternative<mh::Ack>(m);
      });
  const bool saw_candidate =
      std::any_of(replies.begin(), replies.end(), [](const auto &m) {
        return std::holds_alternative<mh::CandidateReply>(m);
      });
  if (!saw_ack || !saw_candidate) {
    std::cerr << "media-host process returned incomplete replies\n";
    return false;
  }

  // A clean child exit exercises the same supervisor restart path as a crash.
  if (!process.Enqueue(mh::Shutdown{}) ||
      !WaitFor([&process] { return !process.healthy(); },
               std::chrono::seconds(3)) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(6))) {
    std::cerr << "media-host process failed bounded restart\n";
    return false;
  }
  if (process.generation() <= first_generation)
    return false;
  process.Stop();
  return !process.healthy() && OwnedDirectories() == before;
}

} // namespace

int main() { return Run() ? 0 : 1; }
