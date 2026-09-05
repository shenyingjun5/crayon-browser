#include "media_observation_cef_message_checks.h"

#include <iostream>

#include "ipc/media_observation_cef_message.h"

namespace {
using namespace crayon::browser::cef_shell::media_ipc;
using namespace crayon::cef_shell::renderer;

#define MEDIA_CHECK(value)                                                     \
  do {                                                                         \
    if (!(value)) {                                                            \
      std::cerr << "media observation codec check failed: " << __LINE__        \
                << '\n';                                                       \
      return false;                                                            \
    }                                                                          \
  } while (false)
} // namespace

bool CheckMediaObservationCefMessages() {
  MediaObservationEnvelope envelope;
  envelope.observation.navigation_id = 7;
  envelope.observation.element_id = 2;
  envelope.observation.source_kind = MediaSourceKind::kHttpUrl;
  envelope.observation.source_url = "https://media.example/video.mp4";
  envelope.observation.playback = MediaPlaybackState::kPaused;
  envelope.observation.element_kind = MediaElementKind::kAudio;
  envelope.source_epoch = 3;
  auto message = CreateObservationMessage(envelope);
  auto decoded = ReadObservationMessage(message);
  MEDIA_CHECK(decoded && decoded->source_epoch == 3 && !decoded->removed);
  MEDIA_CHECK(decoded->observation.element_id == 2 &&
              decoded->observation.element_kind == MediaElementKind::kAudio);
  MEDIA_CHECK(decoded->observation.source_url ==
              envelope.observation.source_url);
  envelope.observation.element_kind = MediaElementKind::kVideo;
  envelope.observation.visible_fraction = 0.75;
  envelope.observation.geometry_supported = true;
  envelope.observation.geometry_x = -20;
  envelope.observation.geometry_y = 40;
  envelope.observation.geometry_width = 320;
  envelope.observation.geometry_height = 180;
  envelope.observation.viewport_width = 800;
  envelope.observation.viewport_height = 600;
  auto geometry_message = CreateObservationMessage(envelope);
  auto geometry = ReadObservationMessage(geometry_message);
  MEDIA_CHECK(geometry && geometry->observation.geometry_supported &&
              geometry->observation.geometry_x == -20 &&
              geometry->observation.viewport_width == 800);
  envelope.observation.element_kind = MediaElementKind::kAudio;
  envelope.observation.visible_fraction = 0;
  envelope.observation.geometry_supported = false;
  envelope.observation.geometry_x = envelope.observation.geometry_y = 0;
  envelope.observation.geometry_width = envelope.observation.geometry_height =
      0;
  envelope.observation.viewport_width = envelope.observation.viewport_height =
      0;
  auto values = message->GetArgumentList();
  MEDIA_CHECK(values->GetSize() == 18);
  values->SetString(8, "0");
  MEDIA_CHECK(!ReadObservationMessage(message));
  values->SetString(8, "18446744073709551616");
  MEDIA_CHECK(!ReadObservationMessage(message));
  values->SetInt(8, 3);
  MEDIA_CHECK(!ReadObservationMessage(message));
  values->SetString(8, "3");
  values->SetBool(9, true);
  MEDIA_CHECK(!ReadObservationMessage(message)); // Non-canonical removal.
  values->SetBool(9, false);
  // CefListValue itself CHECKs non-finite values; exercise a representable
  // invalid sample here. Non-finite proof facts have separate pure unit tests.
  values->SetDouble(6, -1);
  MEDIA_CHECK(!ReadObservationMessage(message));
  values->SetDouble(6, 0);
  values->SetString(4, std::string(kMaxSourceUrlLen + 1, 'x'));
  MEDIA_CHECK(!ReadObservationMessage(message));
  values->SetString(4, envelope.observation.source_url);
  values->SetInt(10, 2);
  MEDIA_CHECK(!ReadObservationMessage(message));
  values->SetInt(10, static_cast<int>(MediaElementKind::kAudio));
  values->SetBool(11, true);
  values->SetDouble(14, 320);
  values->SetDouble(15, 180);
  values->SetDouble(16, 800);
  values->SetDouble(17, 600);
  MEDIA_CHECK(!ReadObservationMessage(message));  // Audio is unsupported.
  values->SetBool(11, false);
  MEDIA_CHECK(!ReadObservationMessage(message));  // Non-canonical zeros.
  values->SetDouble(14, 0);
  values->SetDouble(15, 0);
  values->SetDouble(16, 0);
  values->SetDouble(17, 0);
  MEDIA_CHECK(ReadObservationMessage(message));
  values->SetSize(19);
  MEDIA_CHECK(!ReadObservationMessage(message));
  envelope.removed = true;
  envelope.observation.source_kind = MediaSourceKind::kUnknown;
  envelope.observation.source_url.clear();
  envelope.observation.playback = MediaPlaybackState::kIdle;
  message = CreateObservationMessage(envelope);
  decoded = ReadObservationMessage(message);
  MEDIA_CHECK(decoded && decoded->removed && decoded->source_epoch == 3);
  auto old = CefProcessMessage::Create("crayon.media.observation.v1");
  old->GetArgumentList()->SetSize(8);
  MEDIA_CHECK(!ReadObservationMessage(old));
  auto old_v2 = CefProcessMessage::Create("crayon.media.observation.v2");
  old_v2->GetArgumentList()->SetSize(10);
  MEDIA_CHECK(!ReadObservationMessage(old_v2));
  auto old_v3 = CefProcessMessage::Create("crayon.media.observation.v3");
  old_v3->GetArgumentList()->SetSize(11);
  MEDIA_CHECK(!ReadObservationMessage(old_v3));
  MEDIA_CHECK(!ReadObservationMessage(nullptr));
  return true;
}
