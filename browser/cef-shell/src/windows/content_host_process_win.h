#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "crayon/cef_shell_ipc/content_host_codec.h"

namespace crayon::browser::cef_shell::windows {

namespace content_host_ipc = ::crayon::cef_shell::ipc::content_host;

class ContentHostTransport {
 public:
  virtual ~ContentHostTransport() = default;
  virtual bool Start(std::string executable_path) = 0;
  virtual void Stop() = 0;
  virtual bool Enqueue(content_host_ipc::Message message) = 0;
  virtual std::vector<content_host_ipc::Message> Drain(
      std::size_t max_messages) = 0;
  virtual bool healthy() const noexcept = 0;
};

// Windows-only owner for the CNT content host. Process, pipe and health I/O
// stay on a private worker; the Browser UI thread sees bounded queues only.
class ContentHostProcess final : public ContentHostTransport {
 public:
  ContentHostProcess();
  ~ContentHostProcess();

  ContentHostProcess(const ContentHostProcess &) = delete;
  ContentHostProcess &operator=(const ContentHostProcess &) = delete;

  bool Start(std::string executable_path) override;
  void Stop() override;
  bool Enqueue(content_host_ipc::Message message) override;
  std::vector<content_host_ipc::Message> Drain(
      std::size_t max_messages) override;
  bool healthy() const noexcept override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::windows
