#pragma once

#include <map>
#include <string>

#include "include/cef_browser.h"
#include "include/cef_process_message.h"

namespace crayon::browser::cef_shell::renderer {

// Renderer-process CEF adapter. It accepts only Browser-process requests for
// the addressed main frame and keeps DOM objects scoped to VisitDOM.
class CefPageSnapshotRenderer final {
 public:
  CefPageSnapshotRenderer();
  ~CefPageSnapshotRenderer();

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message);
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame);
  void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser);

 private:
  class Session;
  friend class Session;
  void CompleteSession(const std::string& request_id);

  std::map<std::string, CefRefPtr<Session>> sessions_;
};

}  // namespace crayon::browser::cef_shell::renderer
