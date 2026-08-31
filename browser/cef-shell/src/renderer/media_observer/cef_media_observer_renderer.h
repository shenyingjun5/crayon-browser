#pragma once

#include <cstdint>
#include <map>

#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"
#include "renderer/media_observer/media_observer.h"

namespace crayon::browser::cef_shell::renderer {

// Fixed CEF renderer adapter for CEF-09. Page values remain untrusted; this
// class only normalizes and bounds them before the Browser-process proof gate.
class CefMediaObserverRenderer final {
 public:
  CefMediaObserverRenderer();
  ~CefMediaObserverRenderer();

  void OnWebKitInitialized();
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame);
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame);
  void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser);
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message);

 private:
  class NativeHandler;
  friend class NativeHandler;

  struct BrowserState {
    BrowserState() : observer(1) {}
    std::uint64_t navigation_id = 0;
    ::crayon::cef_shell::renderer::MediaObserver observer;
  };

  bool HandleNativeObservation(const CefV8ValueList& arguments,
                               CefString* exception);
  void InstallCollector(CefRefPtr<CefFrame> frame);

  std::map<int, BrowserState> browsers_;
  CefRefPtr<NativeHandler> native_handler_;
};

}  // namespace crayon::browser::cef_shell::renderer
