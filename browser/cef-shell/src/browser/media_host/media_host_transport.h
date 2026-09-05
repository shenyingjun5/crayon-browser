#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "crayon/cef_shell_ipc/media_host_codec.h"
#include "crayon/cef_shell_ipc/media_host_v2_codec.h"

namespace crayon::browser::cef_shell::media_host {

namespace ipc = ::crayon::cef_shell::ipc::media_host;
namespace ipc_v2 = ::crayon::cef_shell::ipc::media_host_v2;

class MediaHostTransport {
public:
  virtual ~MediaHostTransport() = default;
  virtual bool Start(std::string executable_path) = 0;
  virtual void Stop() = 0;
  virtual bool Enqueue(ipc::Message message) = 0;
  virtual bool EnqueuePlayer(ipc_v2::PlayerMessage) { return false; }
  virtual bool EnqueuePlayerList(ipc_v2::PlayerListRequest) { return false; }
  virtual std::vector<ipc_v2::PlayerPageReply> DrainPlayerPages(std::size_t) {
    return {};
  }
  virtual bool EnqueueDraft(ipc_v2::DraftCommand) { return false; }
  virtual std::vector<ipc_v2::DraftStateReply> DrainDraftStates(std::size_t) {
    return {};
  }
  virtual bool supports_player_messages() const noexcept { return false; }
  virtual bool supports_drafts() const noexcept { return false; }
  virtual bool supports_connect() const noexcept { return false; }
  virtual std::uint64_t player_session_id() const noexcept { return 0; }
  virtual std::vector<ipc::Message> Drain(std::size_t max_messages) = 0;
  virtual bool healthy() const noexcept = 0;
  virtual std::uint64_t generation() const noexcept = 0;
};

} // namespace crayon::browser::cef_shell::media_host
