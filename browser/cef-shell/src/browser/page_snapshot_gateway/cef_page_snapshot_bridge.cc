#include "browser/page_snapshot_gateway/cef_page_snapshot_bridge.h"

#include <string>
#include <utility>

#include "include/wrapper/cef_helpers.h"
#include "ipc/page_snapshot_cef_message.h"

namespace crayon::browser::cef_shell::gateway {
namespace {

using ::crayon::cef_shell::gateway::IpcSourceKind;

template <typename Id>
std::optional<Id> MakeId(const std::string& prefix, std::uint64_t value) {
  return Id::TryCreate(prefix + std::to_string(value));
}

}  // namespace

std::optional<browser_engine::SnapshotRequestId>
CefPageSnapshotBridge::StartSnapshot(CefRefPtr<CefBrowser> browser,
                                     std::uint64_t tab_id,
                                     std::uint64_t navigation_id,
                                     browser_engine::SnapshotMode mode) {
  CEF_REQUIRE_UI_THREAD();
  if (shut_down_ || !browser || tab_id == 0 || navigation_id == 0 ||
      next_request_id_ == 0) {
    return std::nullopt;
  }
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame || !frame->IsMain()) return std::nullopt;
  auto request_id = MakeId<browser_engine::SnapshotRequestId>(
      "snapshot-", next_request_id_++);
  auto engine_tab_id = MakeId<browser_engine::TabId>("tab-", tab_id);
  auto expected_url =
      browser_engine::BrowserUrl::TryParse(frame->GetURL().ToString());
  if (!request_id || !engine_tab_id || !expected_url) return std::nullopt;
  browser_engine::SnapshotRequest request{
      *request_id, *engine_tab_id,
      browser_engine::NavigationId::FromRaw(navigation_id), mode};
  RendererSource source{IpcSourceKind::kRenderer,
                        static_cast<std::uint64_t>(browser->GetIdentifier()),
                        frame->GetIdentifier().ToString(), true};
  if (gateway_.BeginRequest(request, source, *expected_url) !=
      SnapshotGatewayResult::kAccepted) {
    return std::nullopt;
  }
  active_.emplace(*request_id, ActiveCefRequest{*engine_tab_id, frame});
  if (observer_) observer_->OnSnapshotStarted(request);
  frame->SendProcessMessage(PID_RENDERER,
                            snapshot_ipc::CreateRequestMessage(request));
  return request_id;
}

SnapshotGatewayResult CefPageSnapshotBridge::CancelSnapshot(
    const browser_engine::SnapshotRequestId& request_id) {
  CEF_REQUIRE_UI_THREAD();
  const auto active = active_.find(request_id);
  if (active != active_.end()) {
    if (active->second.frame) {
      active->second.frame->SendProcessMessage(
          PID_RENDERER, snapshot_ipc::CreateCancelMessage(request_id));
    }
    active_.erase(active);
  }
  const auto result = gateway_.Cancel(request_id);
  if (result == SnapshotGatewayResult::kAccepted && observer_) {
    observer_->OnSnapshotCancelled(request_id);
  }
  return result;
}

bool CefPageSnapshotBridge::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (!message) return false;
  const std::string name = message->GetName().ToString();
  if (name != snapshot_ipc::kChunkMessageName &&
      name != snapshot_ipc::kTerminalMessageName) {
    return false;
  }
  const RendererSource source = Source(browser, frame, source_process);
  if (name == snapshot_ipc::kChunkMessageName) {
    auto chunk = snapshot_ipc::ReadChunkMessage(message);
    if (chunk) {
      const auto request_id = chunk->request_id;
      const auto result = gateway_.SubmitChunk(source, std::move(*chunk));
      if (result == SnapshotGatewayResult::kRejectedBackpressure ||
          result == SnapshotGatewayResult::kRejectedCapacity) {
        const auto active = active_.find(request_id);
        if (active != active_.end()) {
          if (active->second.frame) {
            active->second.frame->SendProcessMessage(
                PID_RENDERER, snapshot_ipc::CreateCancelMessage(request_id));
          }
          gateway_.Reject(request_id,
                          browser_engine::EngineErrorCode::kCapacityExceeded);
          active_.erase(active);
        }
      }
    }
  } else {
    auto terminal = snapshot_ipc::ReadTerminalMessage(message);
    if (terminal) {
      const auto request_id = terminal->request_id;
      if (gateway_.SubmitTerminal(source, std::move(*terminal)) ==
          SnapshotGatewayResult::kAccepted) {
        active_.erase(request_id);
      }
    }
  }
  return true;
}

void CefPageSnapshotBridge::AdvanceNavigation(CefRefPtr<CefBrowser> /*browser*/,
                                              std::uint64_t tab_id,
                                              std::uint64_t navigation_id) {
  CEF_REQUIRE_UI_THREAD();
  auto engine_tab_id = MakeId<browser_engine::TabId>("tab-", tab_id);
  if (!engine_tab_id || navigation_id == 0) return;
  SendCancelForTab(*engine_tab_id);
  gateway_.AdvanceNavigation(
      *engine_tab_id, browser_engine::NavigationId::FromRaw(navigation_id));
  if (observer_) {
    observer_->OnSnapshotNavigation(
        *engine_tab_id, browser_engine::NavigationId::FromRaw(navigation_id));
  }
  EraseTab(*engine_tab_id);
}

void CefPageSnapshotBridge::CloseBrowser(CefRefPtr<CefBrowser> /*browser*/,
                                         std::uint64_t tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto engine_tab_id = MakeId<browser_engine::TabId>("tab-", tab_id);
  if (!engine_tab_id) return;
  SendCancelForTab(*engine_tab_id);
  gateway_.CloseTab(*engine_tab_id);
  if (observer_) observer_->OnSnapshotClosed(*engine_tab_id);
  EraseTab(*engine_tab_id);
}

void CefPageSnapshotBridge::RendererGone(CefRefPtr<CefBrowser> /*browser*/,
                                         std::uint64_t tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto engine_tab_id = MakeId<browser_engine::TabId>("tab-", tab_id);
  if (!engine_tab_id) return;
  SendCancelForTab(*engine_tab_id);
  gateway_.FailTab(*engine_tab_id,
                   browser_engine::EngineErrorCode::kNavigationFailed);
  if (observer_) observer_->OnSnapshotClosed(*engine_tab_id);
  EraseTab(*engine_tab_id);
}

void CefPageSnapshotBridge::ShutDown() {
  CEF_REQUIRE_UI_THREAD();
  shut_down_ = true;
  active_.clear();
  gateway_.ShutDown();
  if (observer_) observer_->OnSnapshotShutdown();
}

RendererSource CefPageSnapshotBridge::Source(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefProcessId source_process) {
  return RendererSource{
      source_process == PID_RENDERER ? IpcSourceKind::kRenderer
                                     : IpcSourceKind::kOther,
      browser ? static_cast<std::uint64_t>(browser->GetIdentifier()) : 0,
      frame ? frame->GetIdentifier().ToString() : std::string{},
      frame && frame->IsMain()};
}

void CefPageSnapshotBridge::SendCancelForTab(
    const browser_engine::TabId& tab_id) {
  for (const auto& entry : active_) {
    if (entry.second.tab_id == tab_id && entry.second.frame) {
      entry.second.frame->SendProcessMessage(
          PID_RENDERER, snapshot_ipc::CreateCancelMessage(entry.first));
    }
  }
}

void CefPageSnapshotBridge::EraseTab(const browser_engine::TabId& tab_id) {
  for (auto iterator = active_.begin(); iterator != active_.end();) {
    if (iterator->second.tab_id == tab_id) {
      iterator = active_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

}  // namespace crayon::browser::cef_shell::gateway
