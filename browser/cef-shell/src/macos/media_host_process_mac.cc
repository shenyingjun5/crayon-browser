#include "macos/media_host_process_mac.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

#include "browser/core_client/core_client_supervisor.h"
#include "crayon/cef_shell_ipc/ipc_channel_contract.h"

extern char **environ;

namespace crayon::browser::cef_shell::macos {
namespace {

using ::crayon::cef_shell::core_client::CoreClientCommand;
using ::crayon::cef_shell::core_client::CoreClientEvent;
using ::crayon::cef_shell::core_client::CoreClientState;
using ::crayon::cef_shell::ipc::DecodeStatus;
using ::crayon::cef_shell::ipc::FrameCodec;
using ::crayon::cef_shell::ipc::IpcError;
using media_host_ipc::CodecError;
using media_host_ipc::Message;
namespace v2 = ::crayon::cef_shell::ipc::media_host_v2;

constexpr std::uint32_t kCapabilities = v2::kCapMediaRead | v2::kCapDraft |
                                        v2::kCapConnect | v2::kCapReason |
                                        v2::kCapSession;
constexpr auto kHandshakeDeadline = std::chrono::seconds(5);

constexpr std::size_t kMaxOutboundFrames = 64;
constexpr std::size_t kMaxResponseMessages = 64;
constexpr std::size_t kReadBufferBytes = 16 * 1024;
constexpr auto kWorkerInterval = std::chrono::milliseconds(10);
constexpr auto kHealthProbeInterval = std::chrono::seconds(1);
constexpr auto kSpawnHealthDeadline = std::chrono::seconds(5);
constexpr auto kGracefulExitDeadline = std::chrono::milliseconds(500);
constexpr auto kTermExitDeadline = std::chrono::milliseconds(500);
constexpr char kHealthRequest[] = "PING";
constexpr char kHealthReply[] = "PONG";

std::uint64_t MonotonicMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void CloseFd(int *fd) {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

bool SetFdFlags(int fd, int command, int flag) {
  const int current = fcntl(fd, command);
  return current >= 0 &&
         fcntl(fd, command == F_GETFL ? F_SETFL : F_SETFD, current | flag) == 0;
}

bool IsSocket(const std::string &path) {
  struct stat metadata{};
  return lstat(path.c_str(), &metadata) == 0 && S_ISSOCK(metadata.st_mode);
}

struct Child final {
  pid_t pid = -1;
  int input = -1;
  int output = -1;
  std::string directory;
  std::string health_socket;
};

void CleanEndpoint(Child *child) {
  if (IsSocket(child->health_socket))
    unlink(child->health_socket.c_str());
  if (!child->directory.empty())
    rmdir(child->directory.c_str());
  child->health_socket.clear();
  child->directory.clear();
}

bool CreateEndpoint(Child *child) {
  char path[] = "/tmp/crayon-media-XXXXXX";
  char *directory = mkdtemp(path);
  if (!directory || chmod(directory, 0700) != 0)
    return false;
  child->directory = directory;
  child->health_socket = child->directory + "/health.sock";
  sockaddr_un address{};
  if (child->health_socket.size() >= sizeof(address.sun_path)) {
    CleanEndpoint(child);
    return false;
  }
  return true;
}

bool SpawnChild(const std::string &executable, Child *child) {
  if (!CreateEndpoint(child))
    return false;
  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
    for (int *fd :
         {&input_pipe[0], &input_pipe[1], &output_pipe[0], &output_pipe[1]})
      CloseFd(fd);
    CleanEndpoint(child);
    return false;
  }
  for (int fd :
       {input_pipe[0], input_pipe[1], output_pipe[0], output_pipe[1]}) {
    if (!SetFdFlags(fd, F_GETFD, FD_CLOEXEC)) {
      for (int *close_fd :
           {&input_pipe[0], &input_pipe[1], &output_pipe[0], &output_pipe[1]})
        CloseFd(close_fd);
      CleanEndpoint(child);
      return false;
    }
  }
  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    for (int *fd :
         {&input_pipe[0], &input_pipe[1], &output_pipe[0], &output_pipe[1]})
      CloseFd(fd);
    CleanEndpoint(child);
    return false;
  }
  const bool actions_ok =
      posix_spawn_file_actions_adddup2(&actions, input_pipe[0], STDIN_FILENO) ==
          0 &&
      posix_spawn_file_actions_adddup2(&actions, output_pipe[1],
                                       STDOUT_FILENO) == 0 &&
      posix_spawn_file_actions_addclose(&actions, input_pipe[1]) == 0 &&
      posix_spawn_file_actions_addclose(&actions, output_pipe[0]) == 0;
  std::vector<char> executable_arg(executable.begin(), executable.end());
  executable_arg.push_back('\0');
  std::vector<char> socket_arg(child->health_socket.begin(),
                               child->health_socket.end());
  socket_arg.push_back('\0');
  char health_switch[] = "--health-socket";
  char *arguments[] = {executable_arg.data(), health_switch, socket_arg.data(),
                       nullptr};
  pid_t pid = -1;
  const int result = actions_ok
                         ? posix_spawn(&pid, executable.c_str(), &actions,
                                       nullptr, arguments, environ)
                         : EINVAL;
  posix_spawn_file_actions_destroy(&actions);
  CloseFd(&input_pipe[0]);
  CloseFd(&output_pipe[1]);
  if (result != 0) {
    CloseFd(&input_pipe[1]);
    CloseFd(&output_pipe[0]);
    CleanEndpoint(child);
    return false;
  }
  child->pid = pid;
  child->input = input_pipe[1];
  child->output = output_pipe[0];
  if (!SetFdFlags(child->input, F_GETFL, O_NONBLOCK) ||
      !SetFdFlags(child->output, F_GETFL, O_NONBLOCK))
    return false;
#ifdef F_SETNOSIGPIPE
  if (fcntl(child->input, F_SETNOSIGPIPE, 1) != 0)
    return false;
#endif
  return true;
}

bool PollFd(int fd, short events, int timeout_ms) {
  pollfd descriptor{fd, events, 0};
  int result = 0;
  do {
    result = poll(&descriptor, 1, timeout_ms);
  } while (result < 0 && errno == EINTR);
  return result == 1 && (descriptor.revents & events) != 0;
}

bool ProbeHealth(const std::string &path) {
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return false;
  int no_sigpipe = 1;
  const bool configured = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                                     sizeof(no_sigpipe)) == 0 &&
                          SetFdFlags(fd, F_GETFL, O_NONBLOCK);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  int result = configured ? connect(fd, reinterpret_cast<sockaddr *>(&address),
                                    sizeof(address))
                          : -1;
  if (result != 0 && errno == EINPROGRESS && PollFd(fd, POLLOUT, 100)) {
    int socket_error = 0;
    socklen_t size = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) == 0 &&
        socket_error == 0)
      result = 0;
  }
  bool healthy = false;
  if (result == 0 &&
      send(fd, kHealthRequest, sizeof(kHealthRequest) - 1, 0) ==
          static_cast<ssize_t>(sizeof(kHealthRequest) - 1) &&
      PollFd(fd, POLLIN, 100)) {
    char reply[sizeof(kHealthReply) - 1]{};
    healthy = recv(fd, reply, sizeof(reply), 0) ==
                  static_cast<ssize_t>(sizeof(reply)) &&
              std::memcmp(reply, kHealthReply, sizeof(reply)) == 0;
  }
  close(fd);
  return healthy;
}

bool WaitForHealth(const Child &child, const std::atomic<bool> &stopping) {
  const auto deadline = std::chrono::steady_clock::now() + kSpawnHealthDeadline;
  while (!stopping.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    if (ProbeHealth(child.health_socket))
      return true;
    int status = 0;
    if (waitpid(child.pid, &status, WNOHANG) == child.pid)
      return false;
    std::this_thread::sleep_for(kWorkerInterval);
  }
  return false;
}

bool WaitForExit(pid_t pid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD))
      return true;
    if (result < 0 && errno != EINTR)
      return false;
    std::this_thread::sleep_for(kWorkerInterval);
  }
  return false;
}

bool WriteWithDeadline(int fd, const std::vector<std::uint8_t> &bytes,
                       std::size_t offset, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (offset < bytes.size() && std::chrono::steady_clock::now() < deadline) {
    const ssize_t written =
        write(fd, bytes.data() + offset, bytes.size() - offset);
    if (written > 0)
      offset += static_cast<std::size_t>(written);
    else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
             errno != EINTR)
      return false;
    else
      static_cast<void>(PollFd(fd, POLLOUT, 10));
  }
  return offset == bytes.size();
}

void StopChild(Child *child,
               const std::optional<std::vector<std::uint8_t>> &pending,
               std::size_t pending_offset, bool graceful) {
  if (child->pid <= 0) {
    CloseFd(&child->input);
    CloseFd(&child->output);
    CleanEndpoint(child);
    return;
  }
  if (graceful && child->input >= 0) {
    bool ready =
        !pending || WriteWithDeadline(child->input, *pending, pending_offset,
                                      kGracefulExitDeadline);
    CodecError codec_error = CodecError::kInvalidValue;
    auto payload =
        media_host_ipc::Encode(media_host_ipc::Shutdown{}, &codec_error);
    if (ready && payload) {
      const auto frame = FrameCodec::Encode(*payload);
      static_cast<void>(
          WriteWithDeadline(child->input, frame, 0, kGracefulExitDeadline));
    }
  }
  CloseFd(&child->input);
  if (!WaitForExit(child->pid, kGracefulExitDeadline)) {
    kill(child->pid, SIGTERM);
    if (!WaitForExit(child->pid, kTermExitDeadline)) {
      kill(child->pid, SIGKILL);
      int status = 0;
      while (waitpid(child->pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
  child->pid = -1;
  CloseFd(&child->output);
  CleanEndpoint(child);
}

bool IsReply(const Message &message) {
  return std::holds_alternative<media_host_ipc::CandidateReply>(message) ||
         std::holds_alternative<media_host_ipc::DecisionReply>(message) ||
         std::holds_alternative<media_host_ipc::Ack>(message) ||
         std::holds_alternative<media_host_ipc::ErrorReply>(message) ||
         std::holds_alternative<media_host_ipc::DevicePageReply>(message) ||
         std::holds_alternative<media_host_ipc::StartCastReply>(message) ||
         std::holds_alternative<media_host_ipc::ResolveCastCodeReply>(
             message) ||
         std::holds_alternative<media_host_ipc::ControlCastReply>(message) ||
         std::holds_alternative<media_host_ipc::SessionEventsReply>(message);
}

} // namespace

class MediaHostProcess::Impl final {
public:
  ~Impl() { Stop(); }

  bool Start(std::string executable_path) {
    if (executable_path.empty() || executable_path.front() != '/' ||
        worker_.joinable())
      return false;
    executable_path_ = std::move(executable_path);
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { Run(); });
    return true;
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_.store(true, std::memory_order_release);
      ClearSessionLocked();
    }
    wake_.notify_all();
    if (worker_.joinable())
      worker_.join();
    healthy_.store(false, std::memory_order_release);
  }

  bool Enqueue(Message message) {
    const auto expected_generation = generation();
    CodecError error = CodecError::kInvalidValue;
    return QueuePayload(media_host_ipc::Encode(message, &error), 0,
                        expected_generation, 0, 0);
  }

  bool EnqueuePlayer(v2::PlayerMessage message) {
    const auto *fact = std::get_if<v2::PlayerFact>(&message);
    const auto &context =
        fact ? fact->context : std::get<v2::PlayerContext>(message);
    return QueuePayload(v2::EncodePlayerMessage(message), context.session_id,
                        context.host_generation, v2::kCapMediaRead, 0);
  }

  bool EnqueuePlayerList(v2::PlayerListRequest request) {
    return QueuePayload(
        v2::EncodePlayerPageMessage(request), request.context.session_id,
        request.context.host_generation, v2::kCapMediaRead, request.max_items);
  }

  bool EnqueueDraft(v2::DraftCommand command) {
    const auto capabilities =
        v2::kCapDraft | v2::kCapReason | v2::kCapSession |
        (command.action == v2::DraftAction::kConnect ? v2::kCapConnect : 0);
    return QueuePayload(v2::EncodeDraftMessage(command),
                        command.context.session_id,
                        command.context.host_generation, capabilities, 0);
  }

  std::vector<v2::PlayerPageReply> DrainPlayerPages(std::size_t maximum) {
    return DrainQueue(&player_responses_, maximum);
  }

  std::vector<v2::DraftStateReply> DrainDraftStates(std::size_t maximum) {
    return DrainQueue(&draft_responses_, maximum);
  }

  bool supports_player_messages() const noexcept {
    return Supports(v2::kCapMediaRead);
  }
  bool supports_drafts() const noexcept {
    return Supports(v2::kCapDraft | v2::kCapReason | v2::kCapSession);
  }
  bool supports_connect() const noexcept {
    return Supports(v2::kCapDraft | v2::kCapConnect | v2::kCapReason |
                    v2::kCapSession);
  }
  std::uint64_t player_session_id() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return healthy() ? negotiated_.session_id : 0;
  }

  std::vector<Message> Drain(std::size_t maximum) {
    std::vector<Message> result;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(maximum, responses_.size());
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(responses_.front()));
      responses_.pop_front();
    }
    return result;
  }

  bool healthy() const noexcept {
    return healthy_.load(std::memory_order_acquire);
  }

  std::uint64_t generation() const noexcept {
    return generation_.load(std::memory_order_acquire);
  }

private:
  bool Supports(std::uint32_t capabilities) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return healthy() &&
           (negotiated_.capabilities & capabilities) == capabilities;
  }

  bool QueuePayload(std::optional<std::vector<std::uint8_t>> payload,
                    std::uint64_t session, std::uint64_t generation,
                    std::uint32_t capabilities, std::uint16_t page_items) {
    if (!payload || !healthy())
      return false;
    auto frame = FrameCodec::Encode(*payload);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy() || stopping_.load(std::memory_order_acquire) ||
        generation != negotiated_.generation ||
        (capabilities && session != negotiated_.session_id) ||
        (negotiated_.capabilities & capabilities) != capabilities ||
        (capabilities && payload->size() > negotiated_.max_frame_bytes) ||
        page_items > negotiated_.max_page_items)
      return false;
    if (outbound_.size() >= kMaxOutboundFrames) {
      // MHV1 retains its fail-closed restart policy; MHV2 callers own
      // backpressure.
      if (!capabilities) {
        ClearSessionLocked();
        invalidated_.store(true, std::memory_order_release);
        wake_.notify_one();
      }
      return false;
    }
    outbound_.push_back(std::move(frame));
    wake_.notify_one();
    return true;
  }

  template <typename T>
  std::vector<T> DrainQueue(std::deque<T> *queue, std::size_t maximum) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<T> result;
    const auto count = std::min(maximum, queue->size());
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(queue->front()));
      queue->pop_front();
    }
    return result;
  }

  // Called under mutex_: retires admission and all publicly visible session
  // data. Pipe/decoder/pending-write state remains worker-owned and is cleared
  // after IO.
  void ClearSessionLocked() {
    healthy_.store(false, std::memory_order_release);
    negotiated_ = {};
    outbound_.clear();
    responses_.clear();
    player_responses_.clear();
    draft_responses_.clear();
  }

  void Run() {
    ::crayon::cef_shell::core_client::CoreClientSupervisor supervisor;
    static_cast<void>(
        supervisor.Apply(CoreClientCommand::kStart, MonotonicMilliseconds()));
    while (!stopping_.load(std::memory_order_acquire)) {
      if (supervisor.state() == CoreClientState::kSpawning) {
        SpawnAndAdmit(&supervisor);
      } else if (supervisor.state() == CoreClientState::kBackoff) {
        WaitUntil(supervisor.backoff_ready_at_ms());
        static_cast<void>(supervisor.Apply(CoreClientCommand::kTick,
                                           MonotonicMilliseconds()));
      } else if (supervisor.state() == CoreClientState::kFailed) {
        break;
      } else {
        ServiceHealthyChild(&supervisor);
      }
    }
    static_cast<void>(
        supervisor.Apply(CoreClientCommand::kStop, MonotonicMilliseconds()));
    ClearQueues();
    StopChild(&child_, pending_write_, pending_offset_, true);
    pending_write_.reset();
    pending_offset_ = 0;
    decoder_.Reset();
  }

  bool SpawnAndAdmit(
      ::crayon::cef_shell::core_client::CoreClientSupervisor *supervisor) {
    child_ = Child{};
    decoder_.Reset();
    const auto next_generation = generation() + 1;
    const auto session = ++session_nonce_;
    std::optional<v2::Handshake> welcome;
    if (next_generation && session && SpawnChild(executable_path_, &child_) &&
        WaitForHealth(child_, stopping_))
      welcome = ExchangeHandshake(session, next_generation);
    if (!welcome) {
      StopChild(&child_, std::nullopt, 0, false);
      decoder_.Reset();
      supervisor->OnEvent(CoreClientEvent::kSpawnFailed,
                          MonotonicMilliseconds());
      return false;
    }
    pending_write_.reset();
    pending_offset_ = 0;
    invalidated_.store(false, std::memory_order_release);
    last_health_probe_ = std::chrono::steady_clock::now();
    bool admitted = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_.load(std::memory_order_acquire)) {
        negotiated_ = *welcome;
        generation_.store(next_generation, std::memory_order_release);
        healthy_.store(true, std::memory_order_release);
        admitted = true;
      }
    }
    if (admitted)
      supervisor->OnEvent(CoreClientEvent::kSpawnAccepted,
                          MonotonicMilliseconds());
    return admitted;
  }

  std::optional<v2::Handshake> ExchangeHandshake(std::uint64_t session,
                                                 std::uint64_t generation) {
    const v2::Handshake hello{v2::Kind::kHello,   session,
                              generation,         kCapabilities,
                              v2::kMaxFrameBytes, v2::kMaxPageItems};
    auto payload = v2::Encode(hello);
    if (!payload)
      return std::nullopt;
    const auto frame = FrameCodec::Encode(*payload);
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + kHandshakeDeadline;
    while (!stopping_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
      if (offset < frame.size()) {
        const auto written =
            write(child_.input, frame.data() + offset, frame.size() - offset);
        if (written > 0)
          offset += static_cast<std::size_t>(written);
        else if (written == 0 ||
                 (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
          return std::nullopt;
        if (offset < frame.size()) {
          static_cast<void>(
              PollFd(child_.input, POLLOUT, kWorkerInterval.count()));
          continue;
        }
      }
      std::uint8_t bytes[kReadBufferBytes];
      const auto count = read(child_.output, bytes, sizeof(bytes));
      if (count == 0)
        return std::nullopt;
      if (count < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
          return std::nullopt;
        static_cast<void>(
            PollFd(child_.output, POLLIN, kWorkerInterval.count()));
        continue;
      }
      IpcError error = IpcError::kFrameMalformed;
      if (!decoder_.Feed(bytes, static_cast<std::size_t>(count), &error))
        return std::nullopt;
      std::vector<std::uint8_t> response;
      std::uint32_t declared = 0;
      const auto status = decoder_.Take(&response, &declared);
      if (status == DecodeStatus::kIncomplete)
        continue;
      if (status == DecodeStatus::kOversize)
        return std::nullopt;
      auto welcome = v2::Decode(response);
      if (!welcome || !v2::MatchesHello(hello, *welcome))
        return std::nullopt;
      // Keep residual complete/partial frames for the normal reply decoder.
      // A repeated Welcome is not a reply and will invalidate the session.
      return welcome;
    }
    return std::nullopt;
  }

  bool ServiceHealthyChild(
      ::crayon::cef_shell::core_client::CoreClientSupervisor *supervisor) {
    if (invalidated_.exchange(false, std::memory_order_acq_rel)) {
      HandleExited(supervisor);
      return false;
    }
    int status = 0;
    const pid_t wait_result = waitpid(child_.pid, &status, WNOHANG);
    if (wait_result == child_.pid || (wait_result < 0 && errno != EINTR) ||
        !FlushOneFrame() || !ReadReplies()) {
      HandleExited(supervisor);
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_health_probe_ >= kHealthProbeInterval) {
      last_health_probe_ = now;
      if (!ProbeHealth(child_.health_socket)) {
        HandleExited(supervisor);
        return false;
      }
      supervisor->OnEvent(CoreClientEvent::kHealthPinged,
                          MonotonicMilliseconds());
    }
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, kWorkerInterval, [this] {
      return stopping_.load(std::memory_order_acquire) || !outbound_.empty();
    });
    return true;
  }

  bool FlushOneFrame() {
    if (!pending_write_) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (outbound_.empty())
        return true;
      pending_write_ = std::move(outbound_.front());
      outbound_.pop_front();
      pending_offset_ = 0;
    }
    const auto &bytes = *pending_write_;
    const ssize_t written = write(child_.input, bytes.data() + pending_offset_,
                                  bytes.size() - pending_offset_);
    if (written > 0) {
      pending_offset_ += static_cast<std::size_t>(written);
      if (pending_offset_ == bytes.size()) {
        pending_write_.reset();
        pending_offset_ = 0;
      }
      return true;
    }
    return written < 0 &&
           (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
  }

  bool ReadReplies() {
    if (!DecodeReplies())
      return false;
    std::uint8_t bytes[kReadBufferBytes];
    for (std::size_t batch = 0; batch < kMaxResponseMessages &&
                                !stopping_.load(std::memory_order_acquire);
         ++batch) {
      const ssize_t count = read(child_.output, bytes, sizeof(bytes));
      if (count > 0) {
        IpcError error = IpcError::kFrameMalformed;
        if (!decoder_.Feed(bytes, static_cast<std::size_t>(count), &error) ||
            !DecodeReplies())
          return false;
      } else if (count == 0) {
        return false;
      } else {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
      }
    }
    return true;
  }

  bool DecodeReplies() {
    for (;;) {
      std::vector<std::uint8_t> payload;
      std::uint32_t declared = 0;
      const DecodeStatus status = decoder_.Take(&payload, &declared);
      if (status == DecodeStatus::kIncomplete)
        return true;
      if (status == DecodeStatus::kOversize)
        return false;
      auto page = v2::DecodePlayerPageMessage(payload);
      auto draft = v2::DecodeDraftMessage(payload);
      CodecError error = CodecError::kInvalidValue;
      auto message = page || draft ? std::optional<Message>{}
                                   : media_host_ipc::Decode(payload, &error);
      std::lock_guard<std::mutex> lock(mutex_);
      if (!healthy() || stopping_.load(std::memory_order_acquire))
        return false;
      if (page) {
        auto *reply = std::get_if<v2::PlayerPageReply>(&*page);
        if (!reply ||
            !MatchesSession(reply->context.session_id,
                            reply->context.host_generation, v2::kCapMediaRead,
                            payload.size()) ||
            reply->players.size() > negotiated_.max_page_items ||
            player_responses_.size() >= kMaxResponseMessages)
          return false;
        player_responses_.push_back(std::move(*reply));
      } else if (draft) {
        auto *reply = std::get_if<v2::DraftStateReply>(&*draft);
        if (!reply ||
            !MatchesSession(reply->context.session_id,
                            reply->context.host_generation,
                            v2::kCapDraft | v2::kCapReason | v2::kCapSession,
                            payload.size()) ||
            draft_responses_.size() >= kMaxResponseMessages)
          return false;
        draft_responses_.push_back(std::move(*reply));
      } else {
        if (!message || !IsReply(*message) ||
            responses_.size() >= kMaxResponseMessages)
          return false;
        responses_.push_back(std::move(*message));
      }
    }
  }

  // mutex_ held by the reply decoder.
  bool MatchesSession(std::uint64_t session, std::uint64_t generation,
                      std::uint32_t capabilities, std::size_t bytes) const {
    return session == negotiated_.session_id &&
           generation == negotiated_.generation &&
           (negotiated_.capabilities & capabilities) == capabilities &&
           bytes <= negotiated_.max_frame_bytes;
  }

  void HandleExited(
      ::crayon::cef_shell::core_client::CoreClientSupervisor *supervisor) {
    ClearQueues();
    StopChild(&child_, pending_write_, pending_offset_, false);
    pending_write_.reset();
    pending_offset_ = 0;
    decoder_.Reset();
    ClearQueues();
    if (supervisor->OnEvent(CoreClientEvent::kProcessExited,
                            MonotonicMilliseconds()))
      static_cast<void>(supervisor->Apply(CoreClientCommand::kAcknowledgeExit,
                                          MonotonicMilliseconds()));
  }

  void ClearQueues() {
    std::lock_guard<std::mutex> lock(mutex_);
    ClearSessionLocked();
  }

  void WaitUntil(std::uint64_t target_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    const std::uint64_t now = MonotonicMilliseconds();
    if (target_ms > now)
      wake_.wait_for(lock, std::chrono::milliseconds(target_ms - now), [this] {
        return stopping_.load(std::memory_order_acquire);
      });
  }

  std::string executable_path_;
  std::thread worker_;
  std::atomic<bool> stopping_{false}, healthy_{false}, invalidated_{false};
  std::atomic<std::uint64_t> generation_{0};
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::vector<std::uint8_t>> outbound_;
  std::deque<Message> responses_;
  std::deque<v2::PlayerPageReply> player_responses_;
  std::deque<v2::DraftStateReply> draft_responses_;
  v2::Handshake negotiated_{};      // mutex_; zero capabilities until admitted.
  std::uint64_t session_nonce_ = 0; // worker only, never reset across Start.
  Child child_;
  FrameCodec decoder_;
  std::optional<std::vector<std::uint8_t>> pending_write_;
  std::size_t pending_offset_ = 0;
  std::chrono::steady_clock::time_point last_health_probe_{};
};

MediaHostProcess::MediaHostProcess() : impl_(std::make_unique<Impl>()) {}
MediaHostProcess::~MediaHostProcess() = default;
bool MediaHostProcess::Start(std::string path) {
  return impl_->Start(std::move(path));
}
void MediaHostProcess::Stop() { impl_->Stop(); }
bool MediaHostProcess::Enqueue(Message message) {
  return impl_->Enqueue(std::move(message));
}
bool MediaHostProcess::EnqueuePlayer(v2::PlayerMessage message) {
  return impl_->EnqueuePlayer(std::move(message));
}
bool MediaHostProcess::EnqueuePlayerList(v2::PlayerListRequest request) {
  return impl_->EnqueuePlayerList(std::move(request));
}
std::vector<v2::PlayerPageReply>
MediaHostProcess::DrainPlayerPages(std::size_t maximum) {
  return impl_->DrainPlayerPages(maximum);
}
bool MediaHostProcess::EnqueueDraft(v2::DraftCommand command) {
  return impl_->EnqueueDraft(std::move(command));
}
std::vector<v2::DraftStateReply>
MediaHostProcess::DrainDraftStates(std::size_t maximum) {
  return impl_->DrainDraftStates(maximum);
}
bool MediaHostProcess::supports_player_messages() const noexcept {
  return impl_->supports_player_messages();
}
bool MediaHostProcess::supports_drafts() const noexcept {
  return impl_->supports_drafts();
}
bool MediaHostProcess::supports_connect() const noexcept {
  return impl_->supports_connect();
}
std::uint64_t MediaHostProcess::player_session_id() const noexcept {
  return impl_->player_session_id();
}
std::vector<Message> MediaHostProcess::Drain(std::size_t maximum) {
  return impl_->Drain(maximum);
}
bool MediaHostProcess::healthy() const noexcept { return impl_->healthy(); }
std::uint64_t MediaHostProcess::generation() const noexcept {
  return impl_->generation();
}

} // namespace crayon::browser::cef_shell::macos
