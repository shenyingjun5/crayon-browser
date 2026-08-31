#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "crayon/cef_shell_ipc/media_host_codec.h"

namespace crayon::browser::cef_shell::media_host {

namespace ipc = ::crayon::cef_shell::ipc::media_host;

class MediaHostTransport {
 public:
  virtual ~MediaHostTransport() = default;
  virtual bool Start(std::string executable_path) = 0;
  virtual void Stop() = 0;
  virtual bool Enqueue(ipc::Message message) = 0;
  virtual std::vector<ipc::Message> Drain(std::size_t max_messages) = 0;
  virtual bool healthy() const noexcept = 0;
  virtual std::uint64_t generation() const noexcept = 0;
};

}  // namespace crayon::browser::cef_shell::media_host
