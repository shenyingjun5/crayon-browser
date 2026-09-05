#include "windows/media_host_process_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "browser/core_client/core_client_supervisor.h"
#include "crayon/cef_shell_ipc/ipc_channel_contract.h"
#include "crayon/cef_shell_ipc/media_host_v2_codec.h"

namespace crayon::browser::cef_shell::windows {
namespace {

namespace channel_ipc = ::crayon::cef_shell::ipc;
namespace media_host_v2_ipc = ::crayon::cef_shell::ipc::media_host_v2;
namespace core_client = ::crayon::cef_shell::core_client;
using channel_ipc::FrameCodec;
using channel_ipc::IpcError;
using core_client::CoreClientCommand;
using core_client::CoreClientEvent;
using core_client::CoreClientState;
using media_host_ipc::CodecError;
using media_host_ipc::Message;
using media_host_v2_ipc::PlayerMessage;

constexpr std::size_t kMaxFrames = 64;
constexpr std::size_t kReadBytes = 16 * 1024;
constexpr auto kTick = std::chrono::milliseconds(10);
constexpr auto kHealthInterval = std::chrono::seconds(1);
constexpr auto kHealthDeadline = std::chrono::seconds(5);
constexpr auto kHandshakeDeadline = std::chrono::seconds(5);
constexpr DWORD kStopMilliseconds = 500;
constexpr char kPing[] = "PING";
constexpr char kPong[] = "PONG";

std::uint64_t NowMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void Close(HANDLE *handle) {
  if (*handle && *handle != INVALID_HANDLE_VALUE)
    CloseHandle(*handle);
  *handle = nullptr;
}

std::wstring Utf8ToWide(const std::string &value) {
  if (value.empty())
    return {};
  const int count =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), nullptr, 0);
  if (count <= 0)
    return {};
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                             static_cast<int>(value.size()), result.data(),
                             count) == count
             ? result
             : std::wstring{};
}

struct Child final {
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  HANDLE input = nullptr;
  HANDLE output = nullptr;
  std::wstring health_path;
};

bool CreateControlPipes(HANDLE *child_input, HANDLE *parent_input,
                        HANDLE *parent_output, HANDLE *child_output) {
  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  if (!CreatePipe(child_input, parent_input, &attributes, 64 * 1024) ||
      !CreatePipe(parent_output, child_output, &attributes, 64 * 1024)) {
    Close(child_input);
    Close(parent_input);
    Close(parent_output);
    Close(child_output);
    return false;
  }
  const bool configured =
      SetHandleInformation(*parent_input, HANDLE_FLAG_INHERIT, 0) &&
      SetHandleInformation(*parent_output, HANDLE_FLAG_INHERIT, 0);
  if (!configured) {
    Close(child_input);
    Close(parent_input);
    Close(parent_output);
    Close(child_output);
  }
  return configured;
}

bool ConfigureHandleList(STARTUPINFOEXW *startup,
                         std::vector<std::uint8_t> *storage, HANDLE *handles) {
  SIZE_T bytes = 0;
  if (InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes) ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return false;
  }
  storage->resize(bytes);
  startup->lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage->data());
  if (!InitializeProcThreadAttributeList(startup->lpAttributeList, 1, 0,
                                         &bytes)) {
    return false;
  }
  if (!UpdateProcThreadAttribute(startup->lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                 3 * sizeof(HANDLE), nullptr, nullptr)) {
    DeleteProcThreadAttributeList(startup->lpAttributeList);
    startup->lpAttributeList = nullptr;
    return false;
  }
  return true;
}

bool SpawnChild(const std::wstring &executable, std::uint64_t nonce,
                Child *child) {
  HANDLE child_input = nullptr;
  HANDLE child_output = nullptr;
  if (!CreateControlPipes(&child_input, &child->input, &child->output,
                          &child_output)) {
    return false;
  }
  const std::wstring purpose = L"media-health-" +
                               std::to_wstring(GetCurrentProcessId()) + L"-" +
                               std::to_wstring(nonce);
  child->health_path = L"\\\\.\\pipe\\crayon-agent-" + purpose;
  HANDLE null_error =
      CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (null_error == INVALID_HANDLE_VALUE ||
      !SetHandleInformation(null_error, HANDLE_FLAG_INHERIT,
                            HANDLE_FLAG_INHERIT)) {
    Close(&child_input);
    Close(&child_output);
    Close(&child->input);
    Close(&child->output);
    Close(&null_error);
    return false;
  }
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = child_input;
  startup.StartupInfo.hStdOutput = child_output;
  startup.StartupInfo.hStdError = null_error;
  HANDLE inherited[] = {child_input, child_output, null_error};
  std::vector<std::uint8_t> attribute_storage;
  if (!ConfigureHandleList(&startup, &attribute_storage, inherited)) {
    Close(&child_input);
    Close(&child_output);
    Close(&child->input);
    Close(&child->output);
    Close(&null_error);
    return false;
  }
  PROCESS_INFORMATION process{};
  std::wstring command = L"\"" + executable + L"\" --health-pipe " + purpose;
  const BOOL created =
      CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                         EXTENDED_STARTUPINFO_PRESENT,
                     nullptr, nullptr, &startup.StartupInfo, &process);
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  Close(&child_input);
  Close(&child_output);
  Close(&null_error);
  if (!created) {
    Close(&child->input);
    Close(&child->output);
    return false;
  }
  CloseHandle(process.hThread);
  child->process = process.hProcess;
  child->job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!child->job ||
      !SetInformationJobObject(child->job, JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(child->job, child->process)) {
    TerminateProcess(child->process, 1);
    Close(&child->process);
    Close(&child->job);
    Close(&child->input);
    Close(&child->output);
    return false;
  }
  return true;
}

bool ProbeHealth(const std::wstring &path) {
  HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE)
    return false;
  DWORD written = 0;
  DWORD read = 0;
  char reply[sizeof(kPong) - 1]{};
  const bool ok =
      WriteFile(pipe, kPing, sizeof(kPing) - 1, &written, nullptr) &&
      written == sizeof(kPing) - 1 &&
      ReadFile(pipe, reply, sizeof(reply), &read, nullptr) &&
      read == sizeof(reply) &&
      std::equal(std::begin(reply), std::end(reply), kPong);
  CloseHandle(pipe);
  return ok;
}

bool WaitForHealth(const Child &child, const std::atomic<bool> &stopping) {
  const auto deadline = std::chrono::steady_clock::now() + kHealthDeadline;
  while (!stopping.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    if (ProbeHealth(child.health_path))
      return true;
    if (WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0)
      return false;
    std::this_thread::sleep_for(kTick);
  }
  return false;
}

bool WriteAll(HANDLE handle, const std::vector<std::uint8_t> &bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    const DWORD count = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, static_cast<std::size_t>(MAXDWORD)));
    if (!WriteFile(handle, bytes.data() + offset, count, &written, nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}

void StopChild(Child *child, bool graceful) {
  if (!child->process)
    return;
  if (graceful && child->input) {
    CodecError error = CodecError::kInvalidValue;
    if (auto payload =
            media_host_ipc::Encode(media_host_ipc::Shutdown{}, &error)) {
      static_cast<void>(WriteAll(child->input, FrameCodec::Encode(*payload)));
    }
  }
  Close(&child->input);
  if (WaitForSingleObject(child->process, kStopMilliseconds) != WAIT_OBJECT_0) {
    TerminateJobObject(child->job, 1);
    WaitForSingleObject(child->process, kStopMilliseconds);
  }
  Close(&child->output);
  Close(&child->process);
  Close(&child->job);
  child->health_path.clear();
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
    const std::wstring wide = Utf8ToWide(executable_path);
    if (wide.empty() || !std::filesystem::path(wide).is_absolute() ||
        worker_.joinable()) {
      return false;
    }
    executable_ = wide;
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { Run(); });
    return true;
  }

  void Stop() {
    stopping_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (worker_.joinable()) {
      static_cast<void>(CancelSynchronousIo(worker_.native_handle()));
      worker_.join();
    }
    healthy_.store(false, std::memory_order_release);
    player_messages_.store(false, std::memory_order_release);
    draft_messages_.store(false, std::memory_order_release);
    connect_messages_.store(false, std::memory_order_release);
    player_session_id_.store(0, std::memory_order_release);
  }

  bool Enqueue(Message message) {
    CodecError error = CodecError::kInvalidValue;
    auto payload = media_host_ipc::Encode(message, &error);
    if (!payload || !healthy_.load(std::memory_order_acquire))
      return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_.load(std::memory_order_acquire) ||
        outbound_.size() >= kMaxFrames) {
      return false;
    }
    outbound_.push_back(FrameCodec::Encode(*payload));
    wake_.notify_one();
    return true;
  }

  bool EnqueuePlayer(PlayerMessage message) {
    const auto *fact = std::get_if<media_host_v2_ipc::PlayerFact>(&message);
    const auto &context =
        fact ? fact->context
             : std::get<media_host_v2_ipc::PlayerContext>(message);
    auto payload = media_host_v2_ipc::EncodePlayerMessage(message);
    if (!payload || !healthy_.load(std::memory_order_acquire) ||
        !player_messages_.load(std::memory_order_acquire) ||
        context.session_id !=
            player_session_id_.load(std::memory_order_acquire) ||
        context.host_generation != generation_.load(std::memory_order_acquire))
      return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_.load(std::memory_order_acquire) ||
        !player_messages_.load(std::memory_order_acquire) ||
        context.session_id !=
            player_session_id_.load(std::memory_order_acquire) ||
        context.host_generation !=
            generation_.load(std::memory_order_acquire) ||
        outbound_.size() >= kMaxFrames)
      return false;
    outbound_.push_back(FrameCodec::Encode(*payload));
    wake_.notify_one();
    return true;
  }

  bool EnqueuePlayerList(media_host_v2_ipc::PlayerListRequest request) {
    auto payload = media_host_v2_ipc::EncodePlayerPageMessage(request);
    if (!payload || !healthy_.load(std::memory_order_acquire) ||
        !player_messages_.load(std::memory_order_acquire) ||
        request.context.session_id !=
            player_session_id_.load(std::memory_order_acquire) ||
        request.context.host_generation !=
            generation_.load(std::memory_order_acquire))
      return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_.load(std::memory_order_acquire) ||
        !player_messages_.load(std::memory_order_acquire) ||
        request.context.session_id !=
            player_session_id_.load(std::memory_order_acquire) ||
        request.context.host_generation !=
            generation_.load(std::memory_order_acquire) ||
        outbound_.size() >= kMaxFrames)
      return false;
    outbound_.push_back(FrameCodec::Encode(*payload));
    wake_.notify_one();
    return true;
  }

  bool EnqueueDraft(media_host_v2_ipc::DraftCommand command) {
    auto payload = media_host_v2_ipc::EncodeDraftMessage(command);
    const bool connect = command.action == media_host_v2_ipc::DraftAction::kConnect;
    if (!payload || !healthy_.load(std::memory_order_acquire) ||
        !draft_messages_.load(std::memory_order_acquire) ||
        (connect && !connect_messages_.load(std::memory_order_acquire)) ||
        command.context.session_id !=
            player_session_id_.load(std::memory_order_acquire) ||
        command.context.host_generation !=
            generation_.load(std::memory_order_acquire))
      return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!healthy_.load(std::memory_order_acquire) ||
        !draft_messages_.load(std::memory_order_acquire) ||
        (connect && !connect_messages_.load(std::memory_order_acquire)) ||
        command.context.session_id !=
            player_session_id_.load(std::memory_order_acquire) ||
        command.context.host_generation !=
            generation_.load(std::memory_order_acquire) ||
        outbound_.size() >= kMaxFrames)
      return false;
    outbound_.push_back(FrameCodec::Encode(*payload));
    wake_.notify_one();
    return true;
  }

  std::vector<Message> Drain(std::size_t maximum) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(maximum, replies_.size());
    std::vector<Message> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(replies_.front()));
      replies_.pop_front();
    }
    return result;
  }

  std::vector<media_host_v2_ipc::PlayerPageReply>
  DrainPlayerPages(std::size_t maximum) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(maximum, player_replies_.size());
    std::vector<media_host_v2_ipc::PlayerPageReply> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(player_replies_.front()));
      player_replies_.pop_front();
    }
    return result;
  }

  std::vector<media_host_v2_ipc::DraftStateReply>
  DrainDraftStates(std::size_t maximum) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(maximum, draft_replies_.size());
    std::vector<media_host_v2_ipc::DraftStateReply> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(draft_replies_.front()));
      draft_replies_.pop_front();
    }
    return result;
  }

  bool healthy() const noexcept {
    return healthy_.load(std::memory_order_acquire);
  }

  std::uint64_t generation() const noexcept {
    return generation_.load(std::memory_order_acquire);
  }

  bool supports_player_messages() const noexcept {
    return player_messages_.load(std::memory_order_acquire);
  }

  bool supports_drafts() const noexcept {
    return draft_messages_.load(std::memory_order_acquire);
  }

  bool supports_connect() const noexcept {
    return connect_messages_.load(std::memory_order_acquire);
  }

  std::uint64_t player_session_id() const noexcept {
    return player_session_id_.load(std::memory_order_acquire);
  }

private:
  void Run() {
    core_client::CoreClientSupervisor supervisor;
    static_cast<void>(
        supervisor.Apply(CoreClientCommand::kStart, NowMilliseconds()));
    while (!stopping_.load(std::memory_order_acquire)) {
      if (supervisor.state() == CoreClientState::kSpawning) {
        if (!SpawnAndAdmit(&supervisor))
          continue;
      } else if (supervisor.state() == CoreClientState::kBackoff) {
        WaitUntil(supervisor.backoff_ready_at_ms());
        static_cast<void>(
            supervisor.Apply(CoreClientCommand::kTick, NowMilliseconds()));
        continue;
      } else if (supervisor.state() == CoreClientState::kFailed) {
        break;
      }
      if (!Service(&supervisor))
        continue;
    }
    healthy_.store(false, std::memory_order_release);
    StopChild(&child_, true);
    Clear();
  }

  bool SpawnAndAdmit(core_client::CoreClientSupervisor *supervisor) {
    child_ = Child{};
    if (!SpawnChild(executable_, ++nonce_, &child_) ||
        !WaitForHealth(child_, stopping_)) {
      StopChild(&child_, false);
      supervisor->OnEvent(CoreClientEvent::kSpawnFailed, NowMilliseconds());
      return false;
    }
    const std::uint64_t next_generation =
        generation_.load(std::memory_order_acquire) + 1;
    if (next_generation == 0 || !ExchangeHandshake(nonce_, next_generation)) {
      StopChild(&child_, false);
      supervisor->OnEvent(CoreClientEvent::kSpawnFailed, NowMilliseconds());
      return false;
    }
    decoder_.Reset();
    last_health_ = std::chrono::steady_clock::now();
    supervisor->OnEvent(CoreClientEvent::kSpawnAccepted, NowMilliseconds());
    generation_.store(next_generation, std::memory_order_release);
    healthy_.store(true, std::memory_order_release);
    return true;
  }

  bool ExchangeHandshake(std::uint64_t session_id, std::uint64_t generation) {
    const media_host_v2_ipc::Handshake hello{media_host_v2_ipc::Kind::kHello,
                                             session_id,
                                             generation,
                                             media_host_v2_ipc::kCapMediaRead |
                                                 media_host_v2_ipc::kCapDraft |
                                                 media_host_v2_ipc::kCapConnect |
                                                 media_host_v2_ipc::kCapReason |
                                                 media_host_v2_ipc::kCapSession,
                                             media_host_v2_ipc::kMaxFrameBytes,
                                             media_host_v2_ipc::kMaxPageItems};
    auto payload = media_host_v2_ipc::Encode(hello);
    if (!payload || !WriteAll(child_.input, FrameCodec::Encode(*payload)))
      return false;
    FrameCodec handshake_decoder;
    const auto deadline = std::chrono::steady_clock::now() + kHandshakeDeadline;
    while (!stopping_.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
      if (WaitForSingleObject(child_.process, 0) == WAIT_OBJECT_0)
        return false;
      DWORD available = 0;
      if (!PeekNamedPipe(child_.output, nullptr, 0, nullptr, &available,
                         nullptr))
        return false;
      if (available == 0) {
        std::this_thread::sleep_for(kTick);
        continue;
      }
      std::uint8_t bytes[kReadBytes];
      DWORD read = 0;
      if (!ReadFile(child_.output, bytes,
                    static_cast<DWORD>(
                        std::min<std::size_t>(available, sizeof(bytes))),
                    &read, nullptr) ||
          read == 0)
        return false;
      IpcError frame_error = IpcError::kFrameMalformed;
      if (!handshake_decoder.Feed(bytes, read, &frame_error))
        return false;
      std::vector<std::uint8_t> response;
      std::uint32_t declared = 0;
      const auto status = handshake_decoder.Take(&response, &declared);
      if (status == channel_ipc::DecodeStatus::kIncomplete)
        continue;
      if (status == channel_ipc::DecodeStatus::kOversize)
        return false;
      auto welcome = media_host_v2_ipc::Decode(response);
      if (!welcome || !media_host_v2_ipc::MatchesHello(hello, *welcome))
        return false;
      player_messages_.store(
          (welcome->capabilities & media_host_v2_ipc::kCapMediaRead) != 0,
          std::memory_order_release);
      const bool reason_messages =
          (welcome->capabilities & media_host_v2_ipc::kCapReason) != 0;
      const bool session_messages =
          (welcome->capabilities & media_host_v2_ipc::kCapSession) != 0;
      draft_messages_.store(reason_messages && session_messages &&
                                (welcome->capabilities &
                                 media_host_v2_ipc::kCapDraft) != 0,
                            std::memory_order_release);
      connect_messages_.store(
          reason_messages && session_messages &&
              (welcome->capabilities & media_host_v2_ipc::kCapConnect) != 0,
          std::memory_order_release);
      player_session_id_.store(session_id, std::memory_order_release);
      return true;
    }
    return false;
  }

  bool Service(core_client::CoreClientSupervisor *supervisor) {
    if (WaitForSingleObject(child_.process, 0) == WAIT_OBJECT_0 ||
        !FlushOne() || !ReadReplies()) {
      Exited(supervisor);
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_health_ >= kHealthInterval) {
      last_health_ = now;
      if (!ProbeHealth(child_.health_path)) {
        Exited(supervisor);
        return false;
      }
      supervisor->OnEvent(CoreClientEvent::kHealthPinged, NowMilliseconds());
    }
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, kTick, [this] {
      return stopping_.load(std::memory_order_acquire) || !outbound_.empty();
    });
    return true;
  }

  bool FlushOne() {
    std::vector<std::uint8_t> frame;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (outbound_.empty())
        return true;
      frame = std::move(outbound_.front());
      outbound_.pop_front();
    }
    return WriteAll(child_.input, frame);
  }

  bool ReadReplies() {
    for (;;) {
      DWORD available = 0;
      if (!PeekNamedPipe(child_.output, nullptr, 0, nullptr, &available,
                         nullptr)) {
        return false;
      }
      if (available == 0)
        return true;
      std::uint8_t bytes[kReadBytes];
      DWORD read = 0;
      if (!ReadFile(child_.output, bytes,
                    static_cast<DWORD>(
                        std::min<std::size_t>(available, sizeof(bytes))),
                    &read, nullptr) ||
          read == 0) {
        return false;
      }
      IpcError frame_error = IpcError::kFrameMalformed;
      if (!decoder_.Feed(bytes, read, &frame_error) || !Decode())
        return false;
    }
  }

  bool Decode() {
    for (;;) {
      std::vector<std::uint8_t> payload;
      std::uint32_t declared = 0;
      const auto status = decoder_.Take(&payload, &declared);
      if (status == channel_ipc::DecodeStatus::kIncomplete)
        return true;
      if (status == channel_ipc::DecodeStatus::kOversize)
        return false;
      CodecError error = CodecError::kInvalidValue;
      if (auto page = media_host_v2_ipc::DecodePlayerPageMessage(payload)) {
        const auto *reply =
            std::get_if<media_host_v2_ipc::PlayerPageReply>(&*page);
        if (!reply ||
            reply->context.session_id !=
                player_session_id_.load(std::memory_order_acquire) ||
            reply->context.host_generation !=
                generation_.load(std::memory_order_acquire))
          return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (player_replies_.size() >= kMaxFrames)
          return false;
        player_replies_.push_back(*reply);
        continue;
      }
      if (auto draft = media_host_v2_ipc::DecodeDraftMessage(payload)) {
        const auto *reply =
            std::get_if<media_host_v2_ipc::DraftStateReply>(&*draft);
        if (!reply ||
            reply->context.session_id !=
                player_session_id_.load(std::memory_order_acquire) ||
            reply->context.host_generation !=
                generation_.load(std::memory_order_acquire))
          return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (draft_replies_.size() >= kMaxFrames)
          return false;
        draft_replies_.push_back(*reply);
        continue;
      }
      auto message = media_host_ipc::Decode(payload, &error);
      if (!message || !IsReply(*message))
        return false;
      std::lock_guard<std::mutex> lock(mutex_);
      if (replies_.size() >= kMaxFrames)
        return false;
      replies_.push_back(std::move(*message));
    }
  }

  void Exited(core_client::CoreClientSupervisor *supervisor) {
    healthy_.store(false, std::memory_order_release);
    player_messages_.store(false, std::memory_order_release);
    draft_messages_.store(false, std::memory_order_release);
    connect_messages_.store(false, std::memory_order_release);
    player_session_id_.store(0, std::memory_order_release);
    StopChild(&child_, false);
    decoder_.Reset();
    Clear();
    if (supervisor->OnEvent(CoreClientEvent::kProcessExited,
                            NowMilliseconds())) {
      static_cast<void>(supervisor->Apply(CoreClientCommand::kAcknowledgeExit,
                                          NowMilliseconds()));
    }
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    outbound_.clear();
    replies_.clear();
    player_replies_.clear();
    draft_replies_.clear();
  }

  void WaitUntil(std::uint64_t target) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto now = NowMilliseconds();
    if (target > now) {
      wake_.wait_for(lock, std::chrono::milliseconds(target - now), [this] {
        return stopping_.load(std::memory_order_acquire);
      });
    }
  }

  std::wstring executable_;
  std::thread worker_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> healthy_{false};
  std::atomic<bool> player_messages_{false};
  std::atomic<bool> draft_messages_{false};
  std::atomic<bool> connect_messages_{false};
  std::atomic<std::uint64_t> player_session_id_{0};
  std::atomic<std::uint64_t> generation_{0};
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::vector<std::uint8_t>> outbound_;
  std::deque<Message> replies_;
  std::deque<media_host_v2_ipc::PlayerPageReply> player_replies_;
  std::deque<media_host_v2_ipc::DraftStateReply> draft_replies_;
  Child child_;
  FrameCodec decoder_;
  std::chrono::steady_clock::time_point last_health_{};
  std::uint64_t nonce_ = 0;
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

bool MediaHostProcess::EnqueuePlayer(PlayerMessage message) {
  return impl_->EnqueuePlayer(std::move(message));
}

bool MediaHostProcess::EnqueuePlayerList(
    media_host_v2_ipc::PlayerListRequest request) {
  return impl_->EnqueuePlayerList(std::move(request));
}

std::vector<media_host_v2_ipc::PlayerPageReply>
MediaHostProcess::DrainPlayerPages(std::size_t maximum) {
  return impl_->DrainPlayerPages(maximum);
}

bool MediaHostProcess::EnqueueDraft(media_host_v2_ipc::DraftCommand command) {
  return impl_->EnqueueDraft(std::move(command));
}

std::vector<media_host_v2_ipc::DraftStateReply>
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

} // namespace crayon::browser::cef_shell::windows
