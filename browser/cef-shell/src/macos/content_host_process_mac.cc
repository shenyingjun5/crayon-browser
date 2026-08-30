#include "macos/content_host_process_mac.h"

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
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "browser/core_client/core_client_supervisor.h"
#include "crayon/cef_shell_ipc/ipc_channel_contract.h"

extern char** environ;

namespace crayon::browser::cef_shell::macos {
namespace {

using content_host_ipc::CodecError;
using content_host_ipc::Message;
using ::crayon::cef_shell::core_client::CoreClientCommand;
using ::crayon::cef_shell::core_client::CoreClientEvent;
using ::crayon::cef_shell::core_client::CoreClientState;
using ::crayon::cef_shell::ipc::FrameCodec;
using ::crayon::cef_shell::ipc::IpcError;

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

void CloseFd(int* fd) {
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

bool IsSocket(const std::string& path) {
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

void CleanEndpoint(Child* child) {
  if (IsSocket(child->health_socket)) unlink(child->health_socket.c_str());
  if (!child->directory.empty()) rmdir(child->directory.c_str());
  child->health_socket.clear();
  child->directory.clear();
}

bool CreateEndpoint(Child* child) {
  char path[] = "/tmp/crayon-content-XXXXXX";
  char* directory = mkdtemp(path);
  if (!directory || chmod(directory, 0700) != 0) return false;
  child->directory = directory;
  child->health_socket = child->directory + "/health.sock";
  sockaddr_un address{};
  if (child->health_socket.size() >= sizeof(address.sun_path)) {
    CleanEndpoint(child);
    return false;
  }
  return true;
}

bool SpawnChild(const std::string& executable, Child* child) {
  if (!CreateEndpoint(child)) return false;
  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
    CloseFd(&input_pipe[0]);
    CloseFd(&input_pipe[1]);
    CloseFd(&output_pipe[0]);
    CloseFd(&output_pipe[1]);
    CleanEndpoint(child);
    return false;
  }
  for (int fd :
       {input_pipe[0], input_pipe[1], output_pipe[0], output_pipe[1]}) {
    if (!SetFdFlags(fd, F_GETFD, FD_CLOEXEC)) {
      CloseFd(&input_pipe[0]);
      CloseFd(&input_pipe[1]);
      CloseFd(&output_pipe[0]);
      CloseFd(&output_pipe[1]);
      CleanEndpoint(child);
      return false;
    }
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    CloseFd(&input_pipe[0]);
    CloseFd(&input_pipe[1]);
    CloseFd(&output_pipe[0]);
    CloseFd(&output_pipe[1]);
    CleanEndpoint(child);
    return false;
  }
  bool actions_ok =
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
  char* arguments[] = {executable_arg.data(), health_switch, socket_arg.data(),
                       nullptr};
  pid_t pid = -1;
  const int spawn_result = actions_ok
                               ? posix_spawn(&pid, executable.c_str(), &actions,
                                             nullptr, arguments, environ)
                               : EINVAL;
  posix_spawn_file_actions_destroy(&actions);
  CloseFd(&input_pipe[0]);
  CloseFd(&output_pipe[1]);
  if (spawn_result != 0) {
    CloseFd(&input_pipe[1]);
    CloseFd(&output_pipe[0]);
    CleanEndpoint(child);
    return false;
  }
  child->pid = pid;
  child->input = input_pipe[1];
  child->output = output_pipe[0];
  if (!SetFdFlags(child->input, F_GETFL, O_NONBLOCK) ||
      !SetFdFlags(child->output, F_GETFL, O_NONBLOCK)) {
    return false;
  }
#ifdef F_SETNOSIGPIPE
  if (fcntl(child->input, F_SETNOSIGPIPE, 1) != 0) return false;
#endif
  return true;
}

bool PollFd(int fd, short events, int timeout_ms) {
  pollfd descriptor{fd, events, 0};
  int result;
  do {
    result = poll(&descriptor, 1, timeout_ms);
  } while (result < 0 && errno == EINTR);
  return result == 1 && (descriptor.revents & events) != 0;
}

bool ProbeHealth(const std::string& path) {
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  int no_sigpipe = 1;
  const bool configured = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                                     sizeof(no_sigpipe)) == 0 &&
                          SetFdFlags(fd, F_GETFL, O_NONBLOCK);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  int connect_result =
      configured
          ? connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))
          : -1;
  if (connect_result != 0 && errno == EINPROGRESS && PollFd(fd, POLLOUT, 100)) {
    int socket_error = 0;
    socklen_t size = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) == 0 &&
        socket_error == 0) {
      connect_result = 0;
    }
  }
  bool healthy = false;
  if (connect_result == 0 &&
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

bool WaitForHealth(const Child& child, const std::atomic<bool>& stopping) {
  const auto deadline = std::chrono::steady_clock::now() + kSpawnHealthDeadline;
  while (!stopping.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    if (ProbeHealth(child.health_socket)) return true;
    int status = 0;
    if (waitpid(child.pid, &status, WNOHANG) == child.pid) return false;
    std::this_thread::sleep_for(kWorkerInterval);
  }
  return false;
}

bool WaitForExit(pid_t pid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD)) return true;
    if (result < 0 && errno != EINTR) return false;
    std::this_thread::sleep_for(kWorkerInterval);
  }
  return false;
}

bool WriteWithDeadline(int fd, const std::vector<std::uint8_t>& bytes,
                       std::size_t offset, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (offset < bytes.size() && std::chrono::steady_clock::now() < deadline) {
    const ssize_t written =
        write(fd, bytes.data() + offset, bytes.size() - offset);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
    } else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
               errno != EINTR) {
      return false;
    } else if (!PollFd(fd, POLLOUT, 10) && errno != EINTR) {
      continue;
    }
  }
  return offset == bytes.size();
}

void StopChild(Child* child,
               const std::optional<std::vector<std::uint8_t>>& pending,
               std::size_t pending_offset, bool graceful) {
  if (child->pid <= 0) {
    CloseFd(&child->input);
    CloseFd(&child->output);
    CleanEndpoint(child);
    return;
  }
  if (graceful && child->input >= 0) {
    bool stream_ready = true;
    if (pending) {
      stream_ready = WriteWithDeadline(child->input, *pending, pending_offset,
                                       kGracefulExitDeadline);
    }
    CodecError codec_error = CodecError::kInvalidValue;
    auto payload =
        content_host_ipc::Encode(content_host_ipc::Shutdown{}, &codec_error);
    if (stream_ready && payload) {
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

}  // namespace

class ContentHostProcess::Impl final {
 public:
  ~Impl() { Stop(); }

  bool Start(std::string executable_path) {
    if (executable_path.empty() || executable_path.front() != '/' ||
        worker_.joinable()) {
      return false;
    }
    executable_path_ = std::move(executable_path);
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { Run(); });
    return true;
  }

  void Stop() {
    stopping_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    healthy_.store(false, std::memory_order_release);
  }

  bool Enqueue(Message message) {
    CodecError codec_error = CodecError::kInvalidValue;
    auto payload = content_host_ipc::Encode(message, &codec_error);
    if (!payload) {
      invalidated_.store(true, std::memory_order_release);
      wake_.notify_one();
      return false;
    }
    if (!healthy_.load(std::memory_order_acquire)) return false;
    auto frame = FrameCodec::Encode(*payload);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_.load(std::memory_order_acquire)) return false;
    if (outbound_.size() >= kMaxOutboundFrames) {
      invalidated_.store(true, std::memory_order_release);
      wake_.notify_one();
      return false;
    }
    outbound_.push_back(std::move(frame));
    wake_.notify_one();
    return true;
  }

  std::vector<Message> Drain(std::size_t max_messages) {
    std::vector<Message> result;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(max_messages, responses_.size());
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

 private:
  void Run() {
    ::crayon::cef_shell::core_client::CoreClientSupervisor supervisor;
    static_cast<void>(
        supervisor.Apply(CoreClientCommand::kStart, MonotonicMilliseconds()));
    while (!stopping_.load(std::memory_order_acquire)) {
      if (supervisor.state() == CoreClientState::kSpawning) {
        if (!SpawnAndAdmit(&supervisor)) continue;
      }
      if (supervisor.state() == CoreClientState::kBackoff) {
        WaitUntil(supervisor.backoff_ready_at_ms());
        static_cast<void>(supervisor.Apply(CoreClientCommand::kTick,
                                           MonotonicMilliseconds()));
        continue;
      }
      if (supervisor.state() == CoreClientState::kFailed) break;
      if (!ServiceHealthyChild(&supervisor)) continue;
    }
    static_cast<void>(
        supervisor.Apply(CoreClientCommand::kStop, MonotonicMilliseconds()));
    healthy_.store(false, std::memory_order_release);
    StopChild(&child_, pending_write_, pending_offset_, true);
    if (supervisor.OnEvent(CoreClientEvent::kProcessExited,
                           MonotonicMilliseconds())) {
      static_cast<void>(supervisor.Apply(CoreClientCommand::kAcknowledgeExit,
                                         MonotonicMilliseconds()));
    }
    ClearTransportQueues();
  }

  bool SpawnAndAdmit(
      ::crayon::cef_shell::core_client::CoreClientSupervisor* supervisor) {
    child_ = Child{};
    if (!SpawnChild(executable_path_, &child_) ||
        !WaitForHealth(child_, stopping_)) {
      StopChild(&child_, std::nullopt, 0, false);
      supervisor->OnEvent(CoreClientEvent::kSpawnFailed,
                          MonotonicMilliseconds());
      return false;
    }
    decoder_.Reset();
    invalidated_.store(false, std::memory_order_release);
    pending_write_.reset();
    pending_offset_ = 0;
    last_health_probe_ = std::chrono::steady_clock::now();
    supervisor->OnEvent(CoreClientEvent::kSpawnAccepted,
                        MonotonicMilliseconds());
    healthy_.store(true, std::memory_order_release);
    return true;
  }

  bool ServiceHealthyChild(
      ::crayon::cef_shell::core_client::CoreClientSupervisor* supervisor) {
    if (stopping_.load(std::memory_order_acquire)) return false;
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
      if (outbound_.empty()) return true;
      pending_write_ = std::move(outbound_.front());
      outbound_.pop_front();
      pending_offset_ = 0;
    }
    const auto& bytes = *pending_write_;
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
    std::uint8_t bytes[kReadBufferBytes];
    for (;;) {
      const ssize_t count = read(child_.output, bytes, sizeof(bytes));
      if (count > 0) {
        IpcError frame_error = IpcError::kFrameMalformed;
        if (!decoder_.Feed(bytes, static_cast<std::size_t>(count),
                           &frame_error)) {
          return false;
        }
        if (!DecodeAvailableReplies()) return false;
        continue;
      }
      if (count == 0) return false;
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
  }

  bool DecodeAvailableReplies() {
    for (;;) {
      std::vector<std::uint8_t> payload;
      std::uint32_t declared = 0;
      const ::crayon::cef_shell::ipc::DecodeStatus status =
          decoder_.Take(&payload, &declared);
      if (status == ::crayon::cef_shell::ipc::DecodeStatus::kIncomplete)
        return true;
      if (status == ::crayon::cef_shell::ipc::DecodeStatus::kOversize)
        return false;
      CodecError codec_error = CodecError::kInvalidValue;
      auto message = content_host_ipc::Decode(payload, &codec_error);
      if (!message ||
          (!std::holds_alternative<content_host_ipc::MarkdownChunk>(*message) &&
           !std::holds_alternative<content_host_ipc::ErrorReply>(*message))) {
        return false;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (responses_.size() >= kMaxResponseMessages) return false;
      responses_.push_back(std::move(*message));
    }
  }

  void HandleExited(
      ::crayon::cef_shell::core_client::CoreClientSupervisor* supervisor) {
    healthy_.store(false, std::memory_order_release);
    StopChild(&child_, pending_write_, pending_offset_, false);
    pending_write_.reset();
    pending_offset_ = 0;
    decoder_.Reset();
    ClearTransportQueues();
    if (supervisor->OnEvent(CoreClientEvent::kProcessExited,
                            MonotonicMilliseconds())) {
      static_cast<void>(supervisor->Apply(CoreClientCommand::kAcknowledgeExit,
                                          MonotonicMilliseconds()));
    }
  }

  void ClearTransportQueues() {
    std::lock_guard<std::mutex> lock(mutex_);
    outbound_.clear();
    responses_.clear();
  }

  void WaitUntil(std::uint64_t target_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    const std::uint64_t now = MonotonicMilliseconds();
    if (target_ms > now) {
      wake_.wait_for(lock, std::chrono::milliseconds(target_ms - now), [this] {
        return stopping_.load(std::memory_order_acquire);
      });
    }
  }

  std::string executable_path_;
  std::thread worker_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> healthy_{false};
  std::atomic<bool> invalidated_{false};
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::vector<std::uint8_t>> outbound_;
  std::deque<Message> responses_;
  Child child_;
  FrameCodec decoder_;
  std::optional<std::vector<std::uint8_t>> pending_write_;
  std::size_t pending_offset_ = 0;
  std::chrono::steady_clock::time_point last_health_probe_{};
};

ContentHostProcess::ContentHostProcess() : impl_(std::make_unique<Impl>()) {}
ContentHostProcess::~ContentHostProcess() = default;

bool ContentHostProcess::Start(std::string executable_path) {
  return impl_->Start(std::move(executable_path));
}

void ContentHostProcess::Stop() { impl_->Stop(); }

bool ContentHostProcess::Enqueue(Message message) {
  return impl_->Enqueue(std::move(message));
}

std::vector<Message> ContentHostProcess::Drain(std::size_t max_messages) {
  return impl_->Drain(max_messages);
}

bool ContentHostProcess::healthy() const noexcept { return impl_->healthy(); }

}  // namespace crayon::browser::cef_shell::macos
