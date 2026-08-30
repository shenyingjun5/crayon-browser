#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "browser/page_snapshot_gateway/page_snapshot_gateway.h"
#include "browser/page_snapshot_gateway/page_snapshot_observer.h"
#include "include/cef_browser.h"
#include "include/cef_process_message.h"

namespace crayon::browser::cef_shell::gateway {

using RendererSource = ::crayon::cef_shell::gateway::RendererSource;
using SnapshotGatewayEvent = ::crayon::cef_shell::gateway::SnapshotGatewayEvent;
using SnapshotGatewayResult =
    ::crayon::cef_shell::gateway::SnapshotGatewayResult;
using SnapshotGatewayStats = ::crayon::cef_shell::gateway::SnapshotGatewayStats;

// Browser-process CEF adapter. Only trusted Browser code can issue requests;
// renderer messages are admitted against the exact browser/main-frame source.
class CefPageSnapshotBridge final {
 public:
  void SetObserver(PageSnapshotObserver* observer) { observer_ = observer; }
  std::optional<browser_engine::SnapshotRequestId> StartSnapshot(
      CefRefPtr<CefBrowser> browser, std::uint64_t tab_id,
      std::uint64_t navigation_id, browser_engine::SnapshotMode mode);
  SnapshotGatewayResult CancelSnapshot(
      const browser_engine::SnapshotRequestId& request_id);
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message);
  void AdvanceNavigation(CefRefPtr<CefBrowser> browser, std::uint64_t tab_id,
                         std::uint64_t navigation_id);
  void CloseBrowser(CefRefPtr<CefBrowser> browser, std::uint64_t tab_id);
  void RendererGone(CefRefPtr<CefBrowser> browser, std::uint64_t tab_id);
  void ShutDown();

  std::vector<SnapshotGatewayEvent> Drain(std::size_t max_events) {
    return gateway_.Drain(max_events);
  }
  SnapshotGatewayStats stats() const noexcept { return gateway_.stats(); }

 private:
  struct ActiveCefRequest final {
    browser_engine::TabId tab_id;
    CefRefPtr<CefFrame> frame;
  };

  static RendererSource Source(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefProcessId source_process);
  void SendCancelForTab(const browser_engine::TabId& tab_id);
  void EraseTab(const browser_engine::TabId& tab_id);

  ::crayon::cef_shell::gateway::PageSnapshotGateway gateway_;
  std::map<browser_engine::SnapshotRequestId, ActiveCefRequest> active_;
  std::uint64_t next_request_id_ = 1;
  bool shut_down_ = false;
  PageSnapshotObserver* observer_ = nullptr;
};

}  // namespace crayon::browser::cef_shell::gateway
