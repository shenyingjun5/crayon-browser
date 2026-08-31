#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "crayon/cef_shell_ipc/media_host_codec.h"

namespace crayon::browser::cef_shell::macos {

namespace media_host_ipc = ::crayon::cef_shell::ipc::media_host;

class MediaHostTransport {
public:
  virtual ~MediaHostTransport() = default;
  virtual bool Start(std::string executable_path) = 0;
  virtual void Stop() = 0;
  virtual bool Enqueue(media_host_ipc::Message message) = 0;
  virtual std::vector<media_host_ipc::Message>
  Drain(std::size_t max_messages) = 0;
  virtual bool healthy() const noexcept = 0;
  virtual std::uint64_t generation() const noexcept = 0;
};

// macOS owner for the private media-host child. Process, pipe and health I/O
// stay on one worker; Browser callers only touch bounded queues.
class MediaHostProcess final : public MediaHostTransport {
public:
  MediaHostProcess();
  ~MediaHostProcess();
  MediaHostProcess(const MediaHostProcess &) = delete;
  MediaHostProcess &operator=(const MediaHostProcess &) = delete;

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

} // namespace crayon::browser::cef_shell::macos
