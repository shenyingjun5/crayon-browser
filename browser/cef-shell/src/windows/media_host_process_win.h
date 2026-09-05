#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "browser/media_host/media_host_transport.h"

namespace crayon::browser::cef_shell::windows {

namespace media_host_ipc = ::crayon::cef_shell::ipc::media_host;

// Windows owner for the private media-host child. Process, pipe and health I/O
// stay on one worker; Browser callers only touch bounded queues.
class MediaHostProcess final : public media_host::MediaHostTransport {
public:
  MediaHostProcess();
  ~MediaHostProcess() override;

  MediaHostProcess(const MediaHostProcess &) = delete;
  MediaHostProcess &operator=(const MediaHostProcess &) = delete;

  bool Start(std::string executable_path) override;
  void Stop() override;
  bool Enqueue(media_host_ipc::Message message) override;
  bool EnqueuePlayer(media_host::ipc_v2::PlayerMessage message) override;
  bool
  EnqueuePlayerList(media_host::ipc_v2::PlayerListRequest request) override;
  std::vector<media_host::ipc_v2::PlayerPageReply>
  DrainPlayerPages(std::size_t max_messages) override;
  bool EnqueueDraft(media_host::ipc_v2::DraftCommand command) override;
  std::vector<media_host::ipc_v2::DraftStateReply>
  DrainDraftStates(std::size_t max_messages) override;
  bool supports_player_messages() const noexcept override;
  bool supports_drafts() const noexcept override;
  bool supports_connect() const noexcept override;
  std::uint64_t player_session_id() const noexcept override;
  std::vector<media_host_ipc::Message> Drain(std::size_t max_messages) override;
  bool healthy() const noexcept override;
  std::uint64_t generation() const noexcept override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace crayon::browser::cef_shell::windows
