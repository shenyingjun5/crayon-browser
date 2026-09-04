#pragma once

#include <cstdint>
#include <optional>

#include "include/cef_process_message.h"
#include "renderer/media_observer/media_observer.h"

namespace crayon::browser::cef_shell::media_ipc {

inline constexpr char kAdvanceMessageName[] = "crayon.media.advance.v1";
inline constexpr char kObservationMessageName[] = "crayon.media.observation.v2";

struct MediaObservationEnvelope {
  ::crayon::cef_shell::renderer::MediaObservation observation;
  bool eme_encrypted = false;
  std::uint64_t source_epoch = 1;
  bool removed = false;
};

CefRefPtr<CefProcessMessage> CreateAdvanceMessage(std::uint64_t navigation_id);
CefRefPtr<CefProcessMessage> CreateObservationMessage(
    const MediaObservationEnvelope& envelope);

std::optional<std::uint64_t> ReadAdvanceMessage(
    CefRefPtr<CefProcessMessage> message);
std::optional<MediaObservationEnvelope> ReadObservationMessage(
    CefRefPtr<CefProcessMessage> message);

}  // namespace crayon::browser::cef_shell::media_ipc
