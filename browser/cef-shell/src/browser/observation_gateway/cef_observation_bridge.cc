#include "browser/observation_gateway/cef_observation_bridge.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/wrapper/cef_helpers.h"
#include "ipc/media_observation_cef_message.h"

namespace crayon::browser::cef_shell::observation {
namespace {

using ::crayon::cef_shell::gateway::GatewayEvent;
using ::crayon::cef_shell::gateway::GatewayResult;
using ::crayon::cef_shell::input_proof::ProofResult;
using ::crayon::cef_shell::network::NetworkObservation;
using ::crayon::cef_shell::network::NetworkObserveResult;
using ::crayon::cef_shell::network::ResourceKind;
using ::crayon::cef_shell::renderer::ClassifySourceUrl;
using ::crayon::cef_shell::renderer::MediaObservation;
using ::crayon::cef_shell::renderer::MediaPlaybackState;
using ::crayon::cef_shell::renderer::MediaSourceKind;

bool ValidateMediaObservation(MediaObservation* observation) {
  if (!observation || observation->navigation_id == 0 ||
      observation->element_id == 0) {
    return false;
  }
  std::string normalized;
  switch (observation->source_kind) {
    case MediaSourceKind::kHttpUrl:
      if (ClassifySourceUrl(observation->source_url, &normalized) !=
          MediaSourceKind::kHttpUrl) {
        return false;
      }
      observation->source_url = std::move(normalized);
      return true;
    case MediaSourceKind::kBlobUrl:
    case MediaSourceKind::kMediaStream:
    case MediaSourceKind::kUnknown:
      return observation->source_url.empty();
  }
  return false;
}

}  // namespace

CefObservationBridge::CefObservationBridge() = default;

void CefObservationBridge::AdvanceNavigation(CefRefPtr<CefBrowser> browser,
                                             std::uint32_t tab_id,
                                             std::uint64_t navigation_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || tab_id == 0 || navigation_id == 0) return;
  auto [iterator, inserted] = bindings_.try_emplace(browser->GetIdentifier());
  static_cast<void>(inserted);
  static_cast<void>(iterator->second.network_observer.Drain());
  iterator->second.tab_id = tab_id;
  iterator->second.navigation_id = navigation_id;
  iterator->second.main_frame_identifier.clear();
  input_proof_.ForgetTab(tab_id);
  iterator->second.eme_encrypted = false;
  gateway_.AdvanceGeneration(tab_id, navigation_id);
  const std::uint32_t generation = gateway_.GenerationOf(tab_id);
  {
    std::lock_guard<std::mutex> lock(io_bindings_mutex_);
    io_bindings_[browser->GetIdentifier()] = {tab_id, navigation_id};
  }
  BindCurrentMainFrame(browser);
  if (lifecycle_callback_) {
    lifecycle_callback_(tab_id, navigation_id, generation, false);
  }
}

void CefObservationBridge::BindCurrentMainFrame(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) return;
  const auto found = bindings_.find(browser->GetIdentifier());
  if (found == bindings_.end())
    return;
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (frame && frame->IsValid() && frame->IsMain()) {
    const std::string identifier = frame->GetIdentifier().ToString();
    if (!found->second.main_frame_identifier.empty() &&
        found->second.main_frame_identifier != identifier)
      input_proof_.ForgetTab(found->second.tab_id);
    found->second.main_frame_identifier = identifier;
    // A cross-site navigation may replace the renderer after loading starts.
    // Repeat binding for the same frame preserves its facts; a replacement
    // above withdraws old element proofs. Neither path resets protection.
    frame->SendProcessMessage(PID_RENDERER, media_ipc::CreateAdvanceMessage(
                                                found->second.navigation_id));
  }
}

void CefObservationBridge::CloseBrowser(CefRefPtr<CefBrowser> browser,
                                        std::uint32_t tab_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) return;
  static_cast<void>(tab_id);
  const auto found = bindings_.find(browser->GetIdentifier());
  if (found != bindings_.end()) {
    const std::uint32_t bound_tab_id = found->second.tab_id;
    input_proof_.ForgetTab(bound_tab_id);
    gateway_.AdvanceGeneration(bound_tab_id, 0);
    const std::uint32_t generation = gateway_.GenerationOf(bound_tab_id);
    bindings_.erase(found);
    if (lifecycle_callback_) {
      lifecycle_callback_(bound_tab_id, 0, generation, true);
    }
  }
  {
    std::lock_guard<std::mutex> lock(io_bindings_mutex_);
    io_bindings_.erase(browser->GetIdentifier());
  }
  NotifyEventsReady();
}

void CefObservationBridge::SetActiveTab(std::uint32_t tab_id) {
  CEF_REQUIRE_UI_THREAD();
  input_proof_.SetActiveTab(tab_id);
}

void CefObservationBridge::NoteTrustedUserInput(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) return;
  const auto found = bindings_.find(browser->GetIdentifier());
  if (found == bindings_.end()) return;
  input_proof_.NoteUserInput(found->second.tab_id, found->second.navigation_id);
}

bool CefObservationBridge::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (!message || message->GetName() != media_ipc::kObservationMessageName) {
    return false;
  }
  ++diagnostics_.received_total;
  if (source_process != PID_RENDERER || !browser || !frame ||
      !frame->IsMain()) {
    return true;
  }
  auto envelope = media_ipc::ReadObservationMessage(message);
  const auto found = bindings_.find(browser->GetIdentifier());
  if (!envelope || found == bindings_.end() ||
      envelope->observation.navigation_id != found->second.navigation_id ||
      frame->GetIdentifier().ToString() !=
          found->second.main_frame_identifier ||
      !ValidateMediaObservation(&envelope->observation)) {
    return true;
  }
  ++diagnostics_.accepted_current_total;
  const std::uint32_t tab_id = found->second.tab_id;
  const std::uint64_t navigation_id = found->second.navigation_id;
  const MediaObservation &observation = envelope->observation;
  if (envelope->removed) {
    input_proof_.Remove(tab_id, navigation_id, observation.element_id,
                        envelope->source_epoch);
    return true;
  }
  const ProofResult playback_proof =
      input_proof_.Observe(tab_id, observation, envelope->source_epoch);
  if (envelope->eme_encrypted) {
    found->second.eme_encrypted = true;
    found->second.network_observer.AssociateEmeEncrypted(navigation_id);
    NetworkObservation protection_marker;
    protection_marker.navigation_id = navigation_id;
    protection_marker.kind = ResourceKind::kMedia;
    protection_marker.eme_encrypted = true;
    static_cast<void>(
        gateway_.SubmitNetwork(tab_id, navigation_id, protection_marker));
    NotifyEventsReady();
  }
  if (observation.playback != MediaPlaybackState::kPlaying) {
    ++diagnostics_.not_playing_denied_total;
    ++diagnostics_.proof_denied_total;
    return true;
  }
  if (observation.visible_fraction <= 0.0) {
    ++diagnostics_.not_visible_denied_total;
    ++diagnostics_.proof_denied_total;
    return true;
  }
  diagnostics_.last_input_proof_result = playback_proof;
  if (diagnostics_.last_input_proof_result != ProofResult::kEligible) {
    ++diagnostics_.input_proof_denied_total;
    ++diagnostics_.proof_denied_total;
    return true;
  }
  if (gateway_.SubmitMedia(tab_id, navigation_id, observation,
                           found->second.eme_encrypted) ==
      GatewayResult::kAccepted) {
    ++diagnostics_.eligible_total;
    NotifyEventsReady();
  }
  return true;
}

CefRefPtr<CefResourceRequestHandler>
CefObservationBridge::CreateResourceRequestHandler(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefRequest> request,
    CefNetworkResourceCallback callback,
    CefRefPtr<CefBaseRefCounted> callback_owner) {
  CEF_REQUIRE_IO_THREAD();
  if (!browser) return nullptr;
  const auto binding = BindingForIo(browser->GetIdentifier());
  if (!binding) return nullptr;
  return CreateNetworkResourceObserver(browser, request, binding->navigation_id,
                                       std::move(callback),
                                       std::move(callback_owner));
}

void CefObservationBridge::OnNetworkResourceFact(CefNetworkResourceFact fact) {
  CEF_REQUIRE_UI_THREAD();
  const auto found = bindings_.find(fact.browser_id);
  if (found == bindings_.end() ||
      found->second.navigation_id != fact.navigation_id) {
    return;
  }
  const auto result = found->second.network_observer.Observe(
      std::move(fact.observation), fact.present_header_name,
      fact.observed_at_ms);
  if (result == NetworkObserveResult::kAccepted) NotifyEventsReady();
}

std::vector<GatewayEvent> CefObservationBridge::Drain(std::size_t max_events) {
  CEF_REQUIRE_UI_THREAD();
  if (max_events == 0) return {};
  for (auto& entry : bindings_) {
    Binding& binding = entry.second;
    for (auto& observation : binding.network_observer.Drain()) {
      static_cast<void>(gateway_.SubmitNetwork(
          binding.tab_id, observation.navigation_id, observation));
    }
  }
  return gateway_.Drain(max_events);
}

::crayon::cef_shell::gateway::GatewayStats CefObservationBridge::stats() const {
  CEF_REQUIRE_UI_THREAD();
  return gateway_.stats();
}

MediaObservationDiagnostics CefObservationBridge::diagnostics() const {
  CEF_REQUIRE_UI_THREAD();
  return diagnostics_;
}

void CefObservationBridge::SetEventsReadyCallback(
    EventsReadyCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  events_ready_callback_ = std::move(callback);
}

void CefObservationBridge::SetLifecycleCallback(LifecycleCallback callback) {
  CEF_REQUIRE_UI_THREAD();
  lifecycle_callback_ = std::move(callback);
}

std::optional<CefObservationBridge::IoBinding>
CefObservationBridge::BindingForIo(int browser_id) const {
  std::lock_guard<std::mutex> lock(io_bindings_mutex_);
  const auto found = io_bindings_.find(browser_id);
  return found == io_bindings_.end() ? std::nullopt
                                     : std::optional<IoBinding>(found->second);
}

void CefObservationBridge::NotifyEventsReady() {
  if (events_ready_callback_) events_ready_callback_();
}

}  // namespace crayon::browser::cef_shell::observation
