#include "ipc/media_observation_cef_message.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <string>

#include "include/cef_values.h"

namespace crayon::browser::cef_shell::media_ipc {
namespace {

using ::crayon::cef_shell::renderer::MediaObservation;
using ::crayon::cef_shell::renderer::MediaPlaybackState;
using ::crayon::cef_shell::renderer::MediaSourceKind;

constexpr std::size_t kObservationSize = 10;
constexpr std::size_t kMaxIdentityTextLength = 20;

std::optional<std::uint64_t> ParseNavigation(const CefString &value) {
  if (value.empty() || value.length() > kMaxIdentityTextLength)
    return std::nullopt;
  const std::string text = value.ToString();
  std::uint64_t navigation_id = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), navigation_id);
  if (text.empty() || navigation_id == 0 || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return navigation_id;
}

bool HasObservationTypes(CefRefPtr<CefListValue> values) {
  if (!values || values->GetSize() != kObservationSize) return false;
  constexpr CefValueType kTypes[kObservationSize] = {
      VTYPE_STRING, VTYPE_INT,    VTYPE_INT,  VTYPE_INT,    VTYPE_STRING,
      VTYPE_DOUBLE, VTYPE_DOUBLE, VTYPE_BOOL, VTYPE_STRING, VTYPE_BOOL};
  for (std::size_t index = 0; index < kObservationSize; ++index) {
    if (values->GetType(index) != kTypes[index]) return false;
  }
  return true;
}

}  // namespace

CefRefPtr<CefProcessMessage> CreateAdvanceMessage(std::uint64_t navigation_id) {
  auto message = CefProcessMessage::Create(kAdvanceMessageName);
  auto values = message->GetArgumentList();
  values->SetSize(1);
  values->SetString(0, std::to_string(navigation_id));
  return message;
}

CefRefPtr<CefProcessMessage> CreateObservationMessage(
    const MediaObservationEnvelope& envelope) {
  auto message = CefProcessMessage::Create(kObservationMessageName);
  auto values = message->GetArgumentList();
  values->SetSize(kObservationSize);
  values->SetString(0, std::to_string(envelope.observation.navigation_id));
  values->SetInt(1, static_cast<int>(envelope.observation.element_id));
  values->SetInt(2, static_cast<int>(envelope.observation.playback));
  values->SetInt(3, static_cast<int>(envelope.observation.source_kind));
  values->SetString(4, envelope.observation.source_url);
  values->SetDouble(5, envelope.observation.visible_fraction);
  values->SetDouble(6, envelope.observation.current_time_seconds);
  values->SetBool(7, envelope.eme_encrypted);
  values->SetString(8, std::to_string(envelope.source_epoch));
  values->SetBool(9, envelope.removed);
  return message;
}

std::optional<std::uint64_t> ReadAdvanceMessage(
    CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != kAdvanceMessageName) {
    return std::nullopt;
  }
  auto values = message->GetArgumentList();
  if (!values || values->GetSize() != 1 || values->GetType(0) != VTYPE_STRING) {
    return std::nullopt;
  }
  return ParseNavigation(values->GetString(0));
}

std::optional<MediaObservationEnvelope> ReadObservationMessage(
    CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != kObservationMessageName) {
    return std::nullopt;
  }
  auto values = message->GetArgumentList();
  if (!HasObservationTypes(values)) return std::nullopt;
  const auto navigation_id = ParseNavigation(values->GetString(0));
  const auto source_epoch = ParseNavigation(values->GetString(8));
  const int element_id = values->GetInt(1);
  const int playback = values->GetInt(2);
  const int source_kind = values->GetInt(3);
  const double visible = values->GetDouble(5);
  const double current_time = values->GetDouble(6);
  if (!navigation_id || !source_epoch || element_id <= 0 ||
      playback < static_cast<int>(MediaPlaybackState::kIdle) ||
      playback > static_cast<int>(MediaPlaybackState::kEnded) ||
      source_kind < static_cast<int>(MediaSourceKind::kHttpUrl) ||
      source_kind > static_cast<int>(MediaSourceKind::kUnknown) ||
      !std::isfinite(visible) || visible < 0.0 || visible > 1.0 ||
      !std::isfinite(current_time) || current_time < 0.0) {
    return std::nullopt;
  }
  const std::string source_url = values->GetString(4).ToString();
  if (source_url.size() > ::crayon::cef_shell::renderer::kMaxSourceUrlLen) {
    return std::nullopt;
  }
  const bool removed = values->GetBool(9);
  if (removed && (playback != static_cast<int>(MediaPlaybackState::kIdle) ||
                  source_kind != static_cast<int>(MediaSourceKind::kUnknown) ||
                  !source_url.empty() || visible != 0 || current_time != 0 ||
                  values->GetBool(7)))
    return std::nullopt;
  MediaObservation observation;
  observation.navigation_id = *navigation_id;
  observation.element_id = static_cast<std::uint32_t>(element_id);
  observation.playback = static_cast<MediaPlaybackState>(playback);
  observation.source_kind = static_cast<MediaSourceKind>(source_kind);
  observation.source_url = source_url;
  observation.visible_fraction = visible;
  observation.current_time_seconds = current_time;
  return MediaObservationEnvelope{std::move(observation), values->GetBool(7),
                                  *source_epoch, removed};
}

}  // namespace crayon::browser::cef_shell::media_ipc
