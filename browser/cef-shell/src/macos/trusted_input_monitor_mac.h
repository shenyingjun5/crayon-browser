#pragma once

#include <functional>
#include <memory>

namespace crayon::browser::cef_shell::macos {

// AppKit local-event adapter. Only genuine mouse-down events delivered to this
// application are reported; the event is always returned unchanged.
class TrustedInputMonitor final {
 public:
  using Callback = std::function<void()>;

  TrustedInputMonitor();
  ~TrustedInputMonitor();

  bool Start(Callback callback);
  void Stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser::cef_shell::macos
