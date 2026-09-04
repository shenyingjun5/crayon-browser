#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "browser/input_proof/player_input_proof.h"
#include "browser/network_observer/cef_network_observer_adapter.h"
#include "browser/network_observer/network_observer.h"
#include "browser/observation_gateway/observation_gateway.h"
#include "include/cef_browser.h"
#include "include/cef_process_message.h"
#include "include/cef_resource_request_handler.h"

namespace crayon::browser::cef_shell::observation {

struct MediaObservationDiagnostics {
  std::uint64_t received_total = 0;
  std::uint64_t accepted_current_total = 0;
  std::uint64_t proof_denied_total = 0;
  std::uint64_t not_playing_denied_total = 0;
  std::uint64_t not_visible_denied_total = 0;
  std::uint64_t input_proof_denied_total = 0;
  ::crayon::cef_shell::input_proof::ProofResult last_input_proof_result =
      ::crayon::cef_shell::input_proof::ProofResult::kEligible;
  std::uint64_t eligible_total = 0;
};

// Browser-process owner for PLT-M05b1. Renderer media claims and CEF resource
// callbacks are fenced to the current tab/navigation; only the InputProofGate
// can emit an eligible media event.
class CefObservationBridge final {
 public:
  using EventsReadyCallback = std::function<void()>;
  using LifecycleCallback = std::function<void(
      std::uint32_t tab_id, std::uint64_t navigation_id,
      std::uint32_t generation, bool closed)>;

  CefObservationBridge();

  void AdvanceNavigation(CefRefPtr<CefBrowser> browser, std::uint32_t tab_id,
                         std::uint64_t navigation_id);
  void BindCurrentMainFrame(CefRefPtr<CefBrowser> browser);
  void CloseBrowser(CefRefPtr<CefBrowser> browser, std::uint32_t tab_id);
  void SetActiveTab(std::uint32_t tab_id);
  void NoteTrustedUserInput(CefRefPtr<CefBrowser> browser);

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message);
  CefRefPtr<CefResourceRequestHandler> CreateResourceRequestHandler(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefRequest> request,
      CefNetworkResourceCallback callback,
      CefRefPtr<CefBaseRefCounted> callback_owner);
  void OnNetworkResourceFact(CefNetworkResourceFact fact);

  std::vector<::crayon::cef_shell::gateway::GatewayEvent> Drain(
      std::size_t max_events);
  ::crayon::cef_shell::gateway::GatewayStats stats() const;
  MediaObservationDiagnostics diagnostics() const;
  void SetEventsReadyCallback(EventsReadyCallback callback);
  void SetLifecycleCallback(LifecycleCallback callback);

private:
  struct Binding {
    std::uint32_t tab_id = 0;
    std::uint64_t navigation_id = 0;
    std::string main_frame_identifier;
    bool eme_encrypted = false;
    ::crayon::cef_shell::network::NetworkObserver network_observer;
  };

  struct IoBinding {
    std::uint32_t tab_id = 0;
    std::uint64_t navigation_id = 0;
  };

  std::optional<IoBinding> BindingForIo(int browser_id) const;
  void NotifyEventsReady();

  std::map<int, Binding> bindings_;
  mutable std::mutex io_bindings_mutex_;
  std::map<int, IoBinding> io_bindings_;
  ::crayon::cef_shell::input_proof::PlayerInputProof input_proof_;
  ::crayon::cef_shell::gateway::ObservationGateway gateway_;
  EventsReadyCallback events_ready_callback_;
  LifecycleCallback lifecycle_callback_;
  MediaObservationDiagnostics diagnostics_;
};

}  // namespace crayon::browser::cef_shell::observation
