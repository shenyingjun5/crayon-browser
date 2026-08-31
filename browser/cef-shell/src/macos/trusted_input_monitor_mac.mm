#include "macos/trusted_input_monitor_mac.h"

#import <AppKit/AppKit.h>

#include <utility>

namespace crayon::browser::cef_shell::macos {

struct TrustedInputMonitor::Impl {
  id token = nil;
  Callback callback;
};

TrustedInputMonitor::TrustedInputMonitor() : impl_(std::make_unique<Impl>()) {}

TrustedInputMonitor::~TrustedInputMonitor() { Stop(); }

bool TrustedInputMonitor::Start(Callback callback) {
  if (impl_->token != nil || !callback) return false;
  impl_->callback = std::move(callback);
  Impl *impl = impl_.get();
  const NSEventMask mask =
      NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown;
  impl_->token = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                       handler:^NSEvent *(NSEvent *event) {
                                                         if (impl->callback) impl->callback();
                                                         return event;
                                                       }];
  if (impl_->token == nil) impl_->callback = {};
  return impl_->token != nil;
}

void TrustedInputMonitor::Stop() {
  if (impl_->token != nil) {
    [NSEvent removeMonitor:impl_->token];
    impl_->token = nil;
  }
  impl_->callback = {};
}

}  // namespace crayon::browser::cef_shell::macos
