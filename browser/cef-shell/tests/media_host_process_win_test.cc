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
namespace mh2 = crayon::browser::cef_shell::media_host::ipc_v2;

class ScopedHandle final {
public:
  ~ScopedHandle() { Reset(); }
  HANDLE *Put() { return &handle_; }
  HANDLE Get() const { return handle_; }
  void Reset() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
    handle_ = nullptr;
  }

private:
  HANDLE handle_ = nullptr;
};

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
  const std::uint64_t session_id = process.player_session_id();
  if (first_generation == 0 || session_id == 0 ||
      !process.supports_player_messages() || !process.supports_drafts() ||
      !process.supports_connect())
    return false;
  sentinel_write.Reset();
  DWORD available = 0;
  SetLastError(ERROR_SUCCESS);
  if (PeekNamedPipe(sentinel_read.Get(), nullptr, 0, nullptr, &available,
                    nullptr) ||
      GetLastError() != ERROR_BROKEN_PIPE) {
    return false;
  }
  sentinel_read.Reset();
  const mh2::PlayerContext player_context{
      session_id, first_generation, 1, 7, 9, 11, 1};
  const mh2::PlayerPageContext page_context{
      session_id, first_generation, 31, 1, 7, 9};
  auto wrong_player_context = player_context;
  ++wrong_player_context.host_generation;
  auto wrong_page_context = page_context;
  ++wrong_page_context.session_id;
  const mh2::DraftContext draft_context{session_id, first_generation, 41,
                                        "default", 1, 7, 9};
  auto wrong_draft_context = draft_context;
  ++wrong_draft_context.host_generation;
  if (process.EnqueuePlayer(wrong_player_context) ||
      process.EnqueuePlayerList({wrong_page_context, 0, 0, 16}) ||
      process.EnqueueDraft({wrong_draft_context, mh2::DraftAction::kOpen}) ||
      !process.healthy()) {
    return false;
  }
  if (!process.EnqueuePlayer(mh2::PlayerFact{
          player_context, 123, mh2::PlayerSourceKind::kHttpUrl, 1000,
          std::nullopt, false, false, false, true, false, 500000,
          "https://page.example/watch", "https://media.example/video.mp4"}) ||
      !process.EnqueuePlayerList({page_context, 0, 0, 16}) ||
      !process.EnqueueDraft({draft_context, mh2::DraftAction::kOpen}) ||
      !process.Enqueue(mh::Navigation{"nav-1", "tab-1", 7, 9}) ||
      !process.Enqueue(mh::IngestUrl{
          "ingest-1", "tab-1", 7, 9, 123, "https://page.example/watch",
          "https://media.example/video.mp4", mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone, std::nullopt, false}) ||
      !process.Enqueue(mh::ListDevices{"devices-1", std::nullopt, 0}) ||
      !process.Enqueue(mh::PollSessionEvents{"events-1"})) {
    return false;
  }
  std::vector<mh2::PlayerPageReply> player_pages;
  if (!WaitFor(
          [&] {
            auto next = process.DrainPlayerPages(2);
            player_pages.insert(player_pages.end(),
                                std::make_move_iterator(next.begin()),
                                std::make_move_iterator(next.end()));
            return !player_pages.empty();
          },
          std::chrono::seconds(8))) {
    return false;
  }
  const auto &page = player_pages.front();
  if (!(page.context == page_context) || page.snapshot_revision == 0 ||
      page.status != mh2::PlayerPageStatus::kOk || page.offset != 0 ||
      page.next_offset || page.players.size() != 1 ||
      page.players[0].instance_id != 11 ||
      page.players[0].source_revision != 1 ||
      page.players[0].redacted_origin != "https://media.example") {
    return false;
  }
  std::vector<mh2::DraftStateReply> draft_states;
  if (!WaitFor(
          [&] {
            auto next = process.DrainDraftStates(2);
            draft_states.insert(draft_states.end(),
                                std::make_move_iterator(next.begin()),
                                std::make_move_iterator(next.end()));
            return !draft_states.empty();
          },
          std::chrono::seconds(8))) {
    return false;
  }
  if (!(draft_states.front().context == draft_context) ||
      draft_states.front().draft_id == 0 ||
      draft_states.front().draft_revision == 0 ||
      draft_states.front().phase != mh2::DraftPhase::kChoosing ||
      draft_states.front().error != mh2::DraftError::kNone) {
    return false;
  }
  auto stale_context = page_context;
  stale_context.request_id = 32;
  if (!process.EnqueuePlayer(player_context) ||
      !process.EnqueuePlayerList(
          {stale_context, page.snapshot_revision, 0, 16}) ||
      !WaitFor(
          [&] {
            auto next = process.DrainPlayerPages(2);
            player_pages.insert(player_pages.end(),
                                std::make_move_iterator(next.begin()),
                                std::make_move_iterator(next.end()));
            return player_pages.size() == 2;
          },
          std::chrono::seconds(8))) {
    return false;
  }
  if (!(player_pages[1].context == stale_context) ||
      player_pages[1].status != mh2::PlayerPageStatus::kStale ||
      !player_pages[1].players.empty() || player_pages[1].next_offset) {
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
                  [](const auto &message) {
                    return std::holds_alternative<mh::Ack>(message);
                  }) &&
      std::any_of(replies.begin(), replies.end(),
                  [](const auto &message) {
                    return std::holds_alternative<mh::CandidateReply>(message);
                  }) &&
      std::any_of(replies.begin(), replies.end(),
                  [](const auto &message) {
                    return std::holds_alternative<mh::DevicePageReply>(message);
                  }) &&
      std::any_of(replies.begin(), replies.end(), [](const auto &message) {
        return std::holds_alternative<mh::SessionEventsReply>(message);
      });
  if (!complete || process.generation() != first_generation ||
      !process.Enqueue(mh::Shutdown{}) ||
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

} // namespace

int main() { return Run() ? 0 : 1; }
