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

  MediaHostProcess(const MediaHostProcess&) = delete;
  MediaHostProcess& operator=(const MediaHostProcess&) = delete;

  bool Start(std::string executable_path) override;
  void Stop() override;
  bool Enqueue(media_host_ipc::Message message) override;
  std::vector<media_host_ipc::Message> Drain(std::size_t max_messages) override;
  bool healthy() const noexcept override;
  std::uint64_t generation() const noexcept override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::windows
