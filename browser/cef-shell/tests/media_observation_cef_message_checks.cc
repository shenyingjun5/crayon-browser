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
  envelope.source_epoch = 3;
  auto message = CreateObservationMessage(envelope);
  auto decoded = ReadObservationMessage(message);
  MEDIA_CHECK(decoded && decoded->source_epoch == 3 && !decoded->removed);
  MEDIA_CHECK(decoded->observation.element_id == 2);
  MEDIA_CHECK(decoded->observation.source_url ==
              envelope.observation.source_url);
  auto values = message->GetArgumentList();
  MEDIA_CHECK(values->GetSize() == 10);
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
  values->SetSize(11);
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
  MEDIA_CHECK(!ReadObservationMessage(nullptr));
  return true;
}
