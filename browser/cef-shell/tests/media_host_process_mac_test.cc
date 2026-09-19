#include "macos/media_host_process_mac.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>
#include <thread>
#include <variant>

#include "crayon/cef_shell_ipc/ipc_channel_contract.h"

namespace {

using crayon::browser::cef_shell::macos::MediaHostProcess;
namespace mh = crayon::browser::cef_shell::macos::media_host_ipc;
namespace v2 = crayon::cef_shell::ipc::media_host_v2;

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

bool Cleared(MediaHostProcess &process) {
  return !process.healthy() && !process.supports_player_messages() &&
         !process.supports_drafts() && !process.supports_connect() &&
         process.player_session_id() == 0 && process.Drain(64).empty() &&
         process.DrainPlayerPages(64).empty() &&
         process.DrainDraftStates(64).empty();
}

bool ExerciseV2(MediaHostProcess &process) {
  if (!process.supports_player_messages() || !process.supports_drafts() ||
      !process.supports_connect() || process.player_session_id() == 0)
    return false;
  const auto session = process.player_session_id();
  const auto generation = process.generation();
  v2::PlayerFact fact;
  fact.context = {session, generation, 4, 5, 6, 7, 1};
  fact.observed_at_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  fact.has_video = fact.visible = true;
  fact.visible_fraction_ppm = 1000000;
  fact.page_url = "https://page.example/watch";
  fact.media_url = "https://media.example/video.mp4";
  v2::PlayerListRequest list{{session, generation, 1, 4, 5, 6}, 0, 0, 16};
  v2::DraftCommand open;
  open.context = {session, generation, 2, "profile-test", 4, 5, 6};
  auto wrong_fact = fact;
  ++wrong_fact.context.session_id;
  auto wrong_list = list;
  ++wrong_list.context.host_generation;
  auto wrong_open = open;
  ++wrong_open.context.session_id;
  auto oversized_list = list;
  oversized_list.max_items = v2::kMaxPageItems + 1;
  auto invalid_fact = fact;
  invalid_fact.context.instance_id = 0;
  if (process.EnqueuePlayer(wrong_fact) ||
      process.EnqueuePlayerList(wrong_list) ||
      process.EnqueueDraft(wrong_open) ||
      process.EnqueuePlayerList(oversized_list) ||
      process.EnqueuePlayer(invalid_fact) || !process.EnqueuePlayer(fact) ||
      !process.EnqueuePlayerList(list) || !process.EnqueueDraft(open))
    return false;
  std::vector<v2::PlayerPageReply> pages;
  std::vector<v2::DraftStateReply> drafts;
  if (!WaitFor(
          [&] {
            auto p = process.DrainPlayerPages(1);
            auto d = process.DrainDraftStates(1);
            pages.insert(pages.end(), p.begin(), p.end());
            drafts.insert(drafts.end(), d.begin(), d.end());
            return !pages.empty() && !drafts.empty();
          },
          std::chrono::seconds(6)))
    return false;
  if (pages.size() != 1 || !(pages[0].context == list.context) ||
      pages[0].status != v2::PlayerPageStatus::kOk ||
      pages[0].players.size() != 1 || pages[0].players[0].instance_id != 7 ||
      pages[0].players[0].redacted_origin != "https://media.example" ||
      drafts.size() != 1 || !(drafts[0].context == open.context) ||
      drafts[0].phase != v2::DraftPhase::kChoosing ||
      drafts[0].error != v2::DraftError::kNone || drafts[0].draft_id == 0)
    return false;
  // Removing the exact instance mutates the registry; old snapshot is stale.
  list.snapshot_revision = pages[0].snapshot_revision;
  ++list.context.request_id;
  if (!process.EnqueuePlayer(fact.context) || !process.EnqueuePlayerList(list))
    return false;
  pages.clear();
  if (!WaitFor(
          [&] {
            pages = process.DrainPlayerPages(1);
            return !pages.empty();
          },
          std::chrono::seconds(6)) ||
      pages[0].status != v2::PlayerPageStatus::kStale ||
      !pages[0].players.empty())
    return false;
  auto cancel = open;
  cancel.context.request_id += 2;
  cancel.action = v2::DraftAction::kCancel;
  cancel.draft_id = drafts[0].draft_id;
  cancel.draft_revision = drafts[0].draft_revision + 1;
  if (!process.EnqueueDraft(cancel))
    return false;
  drafts.clear();
  return WaitFor(
             [&] {
               drafts = process.DrainDraftStates(1);
               return !drafts.empty();
             },
             std::chrono::seconds(6)) &&
         drafts[0].error == v2::DraftError::kStale;
}

bool HandshakeBoundary() {
  const v2::Handshake hello{
      v2::Kind::kHello, 9, 1, v2::kCapMediaRead, v2::kMaxFrameBytes,
      v2::kMaxPageItems};
  v2::Handshake welcome{hello};
  welcome.kind = v2::Kind::kWelcome;
  if (!v2::MatchesHello(hello, welcome))
    return false;
  auto reject = welcome;
  reject.generation += 1;
  if (v2::MatchesHello(hello, reject))
    return false;
  auto widened = welcome;
  widened.capabilities |= v2::kCapStop;
  return !v2::MatchesHello(hello, widened) &&
         !v2::MatchesHello(hello,
                           v2::Handshake{v2::Kind::kHello, 9, 1, 0, 0, 1});
}

// The same *test* executable is spawned through a private symlink. argv[0]
// locates test-only stage files; production still supplies only
// --health-socket.
constexpr auto kStageTimeout = std::chrono::seconds(6);
constexpr std::uint32_t kReducedFrameBytes = 256;
constexpr std::uint16_t kReducedPageItems = 1;
using crayon::cef_shell::ipc::DecodeStatus;
using crayon::cef_shell::ipc::FrameCodec;
using crayon::cef_shell::ipc::IpcError;

bool Mark(const std::filesystem::path &path,
          const std::string &text = "ready") {
  std::ofstream stream(path);
  stream << text;
  stream.close();
  return !stream.fail();
}

v2::PlayerPageReply FixturePage(const v2::Handshake &hello) {
  v2::PlayerPageReply page;
  page.context = {hello.session_id, hello.generation, 1, 4, 5, 6};
  page.snapshot_revision = 1;
  v2::PlayerProjection player;
  player.instance_id = 7;
  player.source_revision = 1;
  player.redacted_origin = "https://media.example";
  page.players.push_back(player);
  return page;
}

class TestChild {
public:
  TestChild(std::filesystem::path root, std::string socket_path)
      : root_(std::move(root)), socket_path_(std::move(socket_path)) {}
  ~TestChild() {
    if (listener_ >= 0)
      close(listener_);
    unlink(socket_path_.c_str());
  }

  bool Run() {
    signal(SIGPIPE, SIG_IGN);
    listener_ = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (listener_ < 0 || socket_path_.size() >= sizeof(address.sun_path))
      return false;
    std::memcpy(address.sun_path, socket_path_.c_str(),
                socket_path_.size() + 1);
    if (bind(listener_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0 ||
        listen(listener_, 4) != 0)
      return false;
    std::ifstream mode_file(root_ / "mode");
    std::string mode;
    mode_file >> mode;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool welcomed = false, fragment_sent = false, fault_sent = false;
    while (std::chrono::steady_clock::now() < deadline) {
      pollfd fds[] = {{listener_, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
      const int count = poll(fds, 2, 10);
      if (count < 0 && errno != EINTR)
        return false;
      if (fds[0].revents & POLLIN) {
        const int peer = accept(listener_, nullptr, nullptr);
        if (peer < 0)
          return false;
        pollfd request{peer, POLLIN, 0};
        char bytes[4]{};
        const bool ok = poll(&request, 1, 100) == 1 &&
                        read(peer, bytes, sizeof(bytes)) == 4 &&
                        std::memcmp(bytes, "PING", 4) == 0 &&
                        write(peer, "PONG", 4) == 4;
        close(peer);
        if (!ok)
          return false;
      }
      if (fds[1].revents & (POLLIN | POLLHUP)) {
        std::uint8_t bytes[4096];
        const auto size = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (size == 0) {
          // A late valid Welcome is attempted only after Stop closes stdin.
          if (hello_ && !welcomed) {
            const bool sent = Send(welcome_frame_);
            return Mark(Stage("late"), sent ? "sent" : "closed");
          }
          return true;
        }
        IpcError error = IpcError::kFrameMalformed;
        if (size < 0 || !decoder_.Feed(bytes, size, &error))
          return false;
        std::vector<std::uint8_t> payload;
        std::uint32_t declared = 0;
        for (;;) {
          const auto status = decoder_.Take(&payload, &declared);
          if (status == DecodeStatus::kIncomplete)
            break;
          if (status != DecodeStatus::kComplete)
            return false;
          if (!hello_) {
            hello_ = v2::Decode(payload);
            if (!hello_ || hello_->kind != v2::Kind::kHello ||
                hello_->max_frame_bytes <= kReducedFrameBytes ||
                hello_->max_page_items <= kReducedPageItems ||
                !(hello_->capabilities & v2::kCapMediaRead))
              return false;
            auto welcome = *hello_;
            welcome.kind = v2::Kind::kWelcome;
            welcome.capabilities = v2::kCapMediaRead;
            welcome.max_frame_bytes = kReducedFrameBytes;
            welcome.max_page_items = kReducedPageItems;
            welcome_frame_ = FrameCodec::Encode(*v2::Encode(welcome));
            page_frame_ = FrameCodec::Encode(
                *v2::EncodePlayerPageMessage(FixturePage(*hello_)));
            if (!Mark(Stage("hello"),
                      socket_path_ + " " + std::to_string(getpid())))
              return false;
          } else if (auto message = mh::Decode(payload, nullptr);
                     message &&
                     std::holds_alternative<mh::Shutdown>(*message)) {
            return true;
          } else if (auto page = v2::DecodePlayerPageMessage(payload);
                     page &&
                     std::holds_alternative<v2::PlayerListRequest>(*page)) {
            if (!Mark(Stage("request")))
              return false;
          }
        }
      }
      if (!hello_)
        continue;
      if (!welcomed && Exists("welcome")) {
        auto combined = welcome_frame_;
        // A single <= PIPE_BUF write includes Welcome and complete/partial
        // reply bytes. The fragment suffix is gated by parent admission.
        const auto residual = mode == "fragment" ? 7 : page_frame_.size();
        combined.insert(combined.end(), page_frame_.begin(),
                        page_frame_.begin() + residual);
        if (!Send(combined) || !Mark(Stage("welcome-sent")))
          return false;
        welcomed = true;
      }
      if (welcomed && mode == "fragment" && !fragment_sent &&
          Exists("suffix")) {
        if (!Send({page_frame_.begin() + 7, page_frame_.end()}))
          return false;
        fragment_sent = true;
      }
      if (welcomed && !fault_sent && Exists("fault")) {
        auto frame = welcome_frame_;
        if (mode != "duplicate") {
          auto page = FixturePage(*hello_);
          if (mode == "wrong-session")
            ++page.context.session_id;
          else if (mode == "page-budget")
            page.players.push_back(page.players.front());
          else if (mode == "frame-budget")
            page.players.front().redacted_origin =
                "https://" + std::string(kReducedFrameBytes, 'a') + ".example";
          else
            return false;
          auto payload = v2::EncodePlayerPageMessage(page);
          if (!payload ||
              (mode == "frame-budget" && payload->size() <= kReducedFrameBytes))
            return false;
          frame = FrameCodec::Encode(*payload);
        }
        if (!Send(frame) || !Mark(Stage("fault-sent")))
          return false;
        fault_sent = true;
      }
    }
    return false;
  }

private:
  std::filesystem::path Stage(const std::string &name) const {
    return root_ / (name + "-" + std::to_string(hello_->session_id));
  }
  bool Exists(const std::string &name) const {
    return std::filesystem::exists(Stage(name));
  }
  bool Send(const std::vector<std::uint8_t> &frame) {
    // All scripted frames fit an atomic pipe write; no bulk output or sleeps.
    return frame.size() <= 4096 &&
           write(STDOUT_FILENO, frame.data(), frame.size()) ==
               static_cast<ssize_t>(frame.size());
  }
  std::filesystem::path root_;
  std::string socket_path_;
  int listener_ = -1;
  FrameCodec decoder_;
  std::optional<v2::Handshake> hello_;
  std::vector<std::uint8_t> welcome_frame_, page_frame_;
};

class ScriptedProcess {
public:
  ScriptedProcess(const std::string &executable, const std::string &mode) {
    char pattern[] = "/tmp/crayon-mhv2-test-XXXXXX";
    if (auto *directory = mkdtemp(pattern)) {
      root = directory;
      std::error_code error;
      std::filesystem::create_symlink(executable, root / "child", error);
      ready = !error && Mark(root / "mode", mode);
    }
  }
  ~ScriptedProcess() {
    process.Stop();
    if (!root.empty()) {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    }
  }
  bool Start() { return ready && process.Start((root / "child").string()); }
  bool Stage(const std::string &name, std::uint64_t session = 1) const {
    return std::filesystem::exists(Path(name, session));
  }
  bool WaitStage(const std::string &name, std::uint64_t session = 1) const {
    return WaitFor([&] { return Stage(name, session); }, kStageTimeout);
  }
  bool Release(const std::string &name, std::uint64_t session = 1) const {
    return Mark(Path(name, session));
  }
  bool EndpointGone(std::uint64_t session) const {
    std::ifstream input(Path("hello", session));
    std::string path;
    pid_t pid = -1;
    input >> path >> pid;
    return !path.empty() && pid > 0 && kill(pid, 0) == -1 && errno == ESRCH &&
           !std::filesystem::exists(path) &&
           !std::filesystem::exists(std::filesystem::path(path).parent_path());
  }
  bool Cleanup() {
    process.Stop();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return !error && !std::filesystem::exists(root);
  }
  MediaHostProcess process;

private:
  std::filesystem::path Path(const std::string &name,
                             std::uint64_t session) const {
    return root / (name + "-" + std::to_string(session));
  }
  std::filesystem::path root;
  bool ready = false;
};

bool ReceiveFixturePage(MediaHostProcess &process) {
  std::vector<v2::PlayerPageReply> pages;
  if (!WaitFor(
          [&] {
            pages = process.DrainPlayerPages(2);
            return !pages.empty();
          },
          kStageTimeout))
    return false;
  v2::Handshake hello;
  hello.session_id = process.player_session_id();
  hello.generation = process.generation();
  return pages.size() == 1 && pages.front() == FixturePage(hello);
}

bool NegotiatedBudgets(MediaHostProcess &process) {
  if (!process.healthy() || !process.supports_player_messages() ||
      process.supports_drafts() || process.supports_connect())
    return false;
  const auto session = process.player_session_id();
  const auto generation = process.generation();
  v2::PlayerListRequest list{
      {session, generation, 2, 4, 5, 6}, 0, 0, kReducedPageItems};
  auto too_many = list;
  ++too_many.max_items;
  v2::PlayerFact fact;
  fact.context = {session, generation, 4, 5, 6, 7, 1};
  fact.observed_at_ms = 1;
  fact.page_url = "https://page.example/";
  fact.media_url = "https://media.example/";
  const auto base = v2::EncodePlayerMessage(fact);
  if (!base || base->size() >= kReducedFrameBytes)
    return false;
  fact.media_url.append(kReducedFrameBytes - base->size(), 'a');
  const auto exact = v2::EncodePlayerMessage(fact);
  if (!exact || exact->size() != kReducedFrameBytes ||
      !process.EnqueuePlayer(fact))
    return false;
  fact.media_url += 'a';
  if (!v2::EncodePlayerMessage(fact))
    return false;
  v2::DraftCommand draft;
  draft.context = {session, generation, 3, "profile-test", 4, 5, 6};
  return !process.EnqueuePlayer(fact) && !process.EnqueuePlayerList(too_many) &&
         !process.EnqueueDraft(draft) && process.EnqueuePlayerList(list);
}

bool ScriptedTransport(const std::string &executable, const std::string &mode) {
  ScriptedProcess fixture(executable, mode);
  auto &process = fixture.process;
  if (!fixture.Start() || !fixture.WaitStage("hello") || !Cleared(process) ||
      !fixture.Release("welcome") ||
      !WaitFor([&] { return process.healthy(); }, kStageTimeout))
    return false;
  if (mode == "fragment") {
    if (!fixture.WaitStage("welcome-sent") ||
        !process.DrainPlayerPages(2).empty() || !fixture.Release("suffix"))
      return false;
  }
  if (!ReceiveFixturePage(process) || !NegotiatedBudgets(process) ||
      !fixture.WaitStage("request"))
    return false;
  const auto generation = process.generation();
  const auto session = process.player_session_id();
  if (mode != "fragment" && mode != "coalesced") {
    if (!fixture.Release("fault") || !fixture.WaitStage("fault-sent") ||
        !WaitFor([&] { return !process.healthy(); }, kStageTimeout) ||
        !Cleared(process) || process.Enqueue(mh::Shutdown{}) ||
        !fixture.WaitStage("hello", session + 1) || !Cleared(process) ||
        !fixture.EndpointGone(session) ||
        !fixture.Release("welcome", session + 1) ||
        !WaitFor([&] { return process.healthy(); }, kStageTimeout) ||
        process.generation() != generation + 1 ||
        process.player_session_id() != session + 1 ||
        process.EnqueuePlayer(
            v2::PlayerContext{session, generation, 4, 5, 6, 7, 1}) ||
        !ReceiveFixturePage(process))
      return false;
  }
  const auto final_session = process.player_session_id();
  process.Stop();
  return Cleared(process) && fixture.EndpointGone(final_session) &&
         fixture.Cleanup();
}

bool StopAfterHello(const std::string &executable) {
  ScriptedProcess fixture(executable, "coalesced");
  auto &process = fixture.process;
  if (!fixture.Start() || !fixture.WaitStage("hello") || !Cleared(process))
    return false;
  const auto generation = process.generation();
  const auto started = std::chrono::steady_clock::now();
  process.Stop();
  process.Stop();
  if (std::chrono::steady_clock::now() - started >= std::chrono::seconds(3) ||
      !fixture.Stage("late") || !fixture.EndpointGone(1) || !Cleared(process) ||
      process.generation() != generation || process.Enqueue(mh::Shutdown{}))
    return false;
  // A new child must handshake again; the late Welcome cannot admit it.
  if (!fixture.Start() || !fixture.WaitStage("hello", 2) || !Cleared(process) ||
      !fixture.Release("welcome", 2) ||
      !WaitFor([&] { return process.healthy(); }, kStageTimeout) ||
      process.generation() != generation + 1 ||
      process.player_session_id() != 2 || !ReceiveFixturePage(process))
    return false;
  process.Stop();
  return Cleared(process) && fixture.EndpointGone(2) && fixture.Cleanup();
}

bool Run() {
  if (!HandshakeBoundary()) {
    std::cerr << "handshake matching checks failed\n";
    return false;
  }

  const auto before = OwnedDirectories();
  MediaHostProcess process;
  if (!Cleared(process) || process.Enqueue(mh::Shutdown{}) ||
      process.Start("relative-child"))
    return false;
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
          mh::HeadersClass::kNone, std::nullopt, false}) ||
      !process.Enqueue(mh::ListDevices{"devices-1", std::nullopt, 0}) ||
      !process.Enqueue(mh::PollSessionEvents{"events-1"}))
    return false;
  std::vector<mh::Message> replies;
  if (!WaitFor(
          [&] {
            auto next = process.Drain(8);
            replies.insert(replies.end(), std::make_move_iterator(next.begin()),
                           std::make_move_iterator(next.end()));
            return replies.size() >= 4;
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
  const bool saw_devices =
      std::any_of(replies.begin(), replies.end(), [](const auto &m) {
        return std::holds_alternative<mh::DevicePageReply>(m);
      });
  const bool saw_events =
      std::any_of(replies.begin(), replies.end(), [](const auto &m) {
        return std::holds_alternative<mh::SessionEventsReply>(m);
      });
  if (!saw_ack || !saw_candidate || !saw_devices || !saw_events) {
    std::cerr << "media-host process returned incomplete replies\n";
    return false;
  }

  if (!ExerciseV2(process)) {
    std::cerr << "media-host MHV2 behavior failed\n";
    return false;
  }
  const auto first_session = process.player_session_id();
  // A clean child exit exercises the same supervisor restart path as a crash.
  if (!process.Enqueue(mh::Shutdown{}) ||
      !WaitFor([&process] { return !process.healthy(); },
               std::chrono::seconds(3)) ||
      !Cleared(process) ||
      !WaitFor([&process] { return process.healthy(); },
               std::chrono::seconds(6))) {
    std::cerr << "media-host process failed bounded restart\n";
    return false;
  }
  if (process.generation() <= first_generation ||
      process.player_session_id() == first_session ||
      process.EnqueuePlayer(
          v2::PlayerContext{first_session, first_generation, 4, 5, 6, 7, 1}) ||
      !process.DrainPlayerPages(64).empty() ||
      !process.DrainDraftStates(64).empty())
    return false;
  process.Stop();
  process.Stop();
  if (!Cleared(process) || !process.Start(CRAYON_MEDIA_HOST_TEST_PATH) ||
      !WaitFor([&] { return process.healthy(); }, std::chrono::seconds(6)) ||
      !ExerciseV2(process))
    return false;
  process.Stop();
  // Immediate cancellation does not guarantee the child reached its handshake.
  const auto stop_start = std::chrono::steady_clock::now();
  if (!process.Start(CRAYON_MEDIA_HOST_TEST_PATH))
    return false;
  process.Stop();
  return Cleared(process) && OwnedDirectories() == before &&
         std::chrono::steady_clock::now() - stop_start <
             std::chrono::seconds(3);
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--health-socket") {
    TestChild child(std::filesystem::path(argv[0]).parent_path(), argv[2]);
    return child.Run() ? 0 : 1;
  }
  const auto executable = std::filesystem::canonical(argv[0]).string();
  for (const auto *mode : {"coalesced", "fragment", "duplicate",
                           "wrong-session", "page-budget", "frame-budget"}) {
    if (!ScriptedTransport(executable, mode)) {
      std::cerr << "scripted transport failed: " << mode << '\n';
      return 1;
    }
    std::cout << "PASS scripted transport: " << mode << '\n';
  }
  if (!StopAfterHello(executable)) {
    std::cerr << "stop after child received Hello failed\n";
    return 1;
  }
  std::cout << "PASS Stop after Hello, late Welcome, cleanup and restart\n";
  return Run() ? 0 : 1;
}
