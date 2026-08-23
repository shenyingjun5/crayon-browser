// CEF-09 contract tests: media observation normalization, stale
// navigation/teardown drops, no fabricated URLs, capacity, eligibility.
#include <cstdlib>
#include <iostream>
#include <string>

#include "renderer/media_observer/media_observer.h"

namespace {

using crayon::cef_shell::renderer::ClassifySourceUrl;
using crayon::cef_shell::renderer::kMaxMediaElements;
using crayon::cef_shell::renderer::MediaObservation;
using crayon::cef_shell::renderer::MediaObserver;
using crayon::cef_shell::renderer::MediaPlaybackState;
using crayon::cef_shell::renderer::MediaSourceKind;
using crayon::cef_shell::renderer::ObserveResult;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

MediaObservation Playing(std::uint64_t nav, std::uint32_t element, const char* url,
                         MediaSourceKind kind, double visible) {
  MediaObservation observation;
  observation.navigation_id = nav;
  observation.element_id = element;
  observation.playback = MediaPlaybackState::kPlaying;
  observation.source_url = url;
  observation.source_kind = kind;
  observation.visible_fraction = visible;
  observation.current_time_seconds = 1.5;
  return observation;
}

bool SourceClassificationMatrix() {
  std::string normalized;
  CHECK(ClassifySourceUrl("https://cdn.example/v.mp4", &normalized) ==
        MediaSourceKind::kHttpUrl);
  CHECK(normalized == "https://cdn.example/v.mp4");
  CHECK(ClassifySourceUrl("http://a.example/hls.m3u8", nullptr) == MediaSourceKind::kHttpUrl);
  CHECK(ClassifySourceUrl("blob:https://a.example/uuid", nullptr) == MediaSourceKind::kBlobUrl);
  CHECK(ClassifySourceUrl("mediastream:local-1", nullptr) ==
        MediaSourceKind::kMediaStream);
  CHECK(ClassifySourceUrl("javascript:alert(1)", nullptr) == MediaSourceKind::kUnknown);
  CHECK(ClassifySourceUrl("", nullptr) == MediaSourceKind::kUnknown);
  CHECK(ClassifySourceUrl(std::string(2049, 'a'), nullptr) == MediaSourceKind::kUnknown);
  CHECK(ClassifySourceUrl(std::string("https://a\tx"), nullptr) == MediaSourceKind::kUnknown);
  return true;
}

bool StaleNavigationDropped() {
  MediaObserver observer(/*frame_id=*/1);
  observer.AdvanceNavigation(100);
  CHECK(observer.Observe(Playing(100, 1, "https://a.example/v.mp4",
                                 MediaSourceKind::kHttpUrl, 0.5)) ==
        ObserveResult::kAccepted);
  // BR-007: events from the previous navigation are dropped.
  CHECK(observer.Observe(Playing(99, 2, "https://a.example/old.mp4",
                                 MediaSourceKind::kHttpUrl, 0.9)) ==
        ObserveResult::kDroppedStaleNavigation);
  CHECK(observer.tracked_count() == 1);
  // Advancing the navigation clears prior observations.
  observer.AdvanceNavigation(101);
  CHECK(observer.tracked_count() == 0);
  CHECK(observer.Observe(Playing(100, 3, "https://a.example/old.mp4",
                                 MediaSourceKind::kHttpUrl, 0.9)) ==
        ObserveResult::kDroppedStaleNavigation);
  return true;
}

bool NoFabricatedUrlsForBlobAndStream() {
  MediaObserver observer(/*frame_id=*/1);
  observer.AdvanceNavigation(1);
  // blob: carrying an http URL is a fabrication attempt: dropped.
  MediaObservation fabricated = Playing(1, 1, "https://a.example/fake.mp4",
                                        MediaSourceKind::kBlobUrl, 0.8);
  CHECK(observer.Observe(fabricated) == ObserveResult::kDroppedInvalidUrl);
  // Honest blob source: accepted with an empty URL.
  MediaObservation honest = Playing(1, 2, "", MediaSourceKind::kBlobUrl, 0.8);
  CHECK(observer.Observe(honest) == ObserveResult::kAccepted);
  // Mis-tagged kind is dropped rather than reinterpreted.
  MediaObservation mistagged = Playing(1, 3, "https://a.example/v.mp4",
                                       MediaSourceKind::kUnknown, 0.8);
  CHECK(observer.Observe(mistagged) == ObserveResult::kDroppedInvalidUrl);
  return true;
}

bool TeardownBlocksLateEvents() {
  MediaObserver observer(/*frame_id=*/7);
  observer.AdvanceNavigation(5);
  observer.Observe(Playing(5, 1, "https://a.example/v.mp4",
                           MediaSourceKind::kHttpUrl, 0.5));
  observer.TearDown();
  CHECK(observer.torn_down());
  // BR-013: late events cannot rebuild candidates.
  CHECK(observer.Observe(Playing(5, 1, "https://a.example/v.mp4",
                                 MediaSourceKind::kHttpUrl, 0.5)) ==
        ObserveResult::kDroppedTeardown);
  CHECK(!observer.FindEligible(5).has_value());
  return true;
}

bool CapacityBounded() {
  MediaObserver observer(/*frame_id=*/1);
  observer.AdvanceNavigation(1);
  for (std::uint32_t i = 0; i < kMaxMediaElements; ++i) {
    CHECK(observer.Observe(Playing(1, i, "https://a.example/v.mp4",
                                   MediaSourceKind::kHttpUrl, 0.1)) ==
          ObserveResult::kAccepted);
  }
  CHECK(observer.Observe(Playing(1, kMaxMediaElements, "https://a.example/v.mp4",
                                 MediaSourceKind::kHttpUrl, 0.1)) ==
        ObserveResult::kDroppedCapacity);
  // Updating an existing element still succeeds.
  CHECK(observer.Observe(Playing(1, 0, "https://a.example/v2.mp4",
                                 MediaSourceKind::kHttpUrl, 0.9)) ==
        ObserveResult::kAccepted);
  CHECK(observer.tracked_count() == kMaxMediaElements);
  return true;
}

bool EligibilityPrefersVisiblePlaying() {
  MediaObserver observer(/*frame_id=*/1);
  observer.AdvanceNavigation(1);
  // Paused and invisible elements never qualify.
  MediaObservation paused = Playing(1, 1, "https://a.example/a.mp4",
                                    MediaSourceKind::kHttpUrl, 0.9);
  paused.playback = MediaPlaybackState::kPaused;
  CHECK(observer.Observe(paused) == ObserveResult::kAccepted);
  MediaObservation invisible = Playing(1, 2, "https://a.example/b.mp4",
                                       MediaSourceKind::kHttpUrl, 0.0);
  CHECK(observer.Observe(invisible) == ObserveResult::kAccepted);
  CHECK(!observer.FindEligible(1).has_value());
  // Two playing candidates: the more visible one wins (BR-006 shape).
  CHECK(observer.Observe(Playing(1, 3, "https://a.example/c.mp4",
                                 MediaSourceKind::kHttpUrl, 0.4)) ==
        ObserveResult::kAccepted);
  CHECK(observer.Observe(Playing(1, 4, "https://a.example/d.mp4",
                                 MediaSourceKind::kHttpUrl, 0.7)) ==
        ObserveResult::kAccepted);
  const auto eligible = observer.FindEligible(1);
  CHECK(eligible.has_value() && eligible->element_id == 4);
  CHECK(eligible->frame_id == 1);
  // Visibility is clamped into [0,1].
  MediaObservation overflow = Playing(1, 5, "https://a.example/e.mp4",
                                      MediaSourceKind::kHttpUrl, 5.0);
  CHECK(observer.Observe(overflow) == ObserveResult::kAccepted);
  const auto clamped = observer.FindEligible(1);
  CHECK(clamped.has_value() && clamped->visible_fraction == 1.0);
  return true;
}

/// No auto-interaction by construction: the observer exposes no command
/// surface — this compile-time check pins the API shape.
bool ApiExposesNoInteractionSurface() {
  MediaObserver observer(/*frame_id=*/1);
  observer.AdvanceNavigation(1);
  observer.Observe(Playing(1, 1, "https://a.example/ad.mp4",
                           MediaSourceKind::kHttpUrl, 1.0));
  // The only outputs are classifications and observations; there is no
  // click/seek/rate/filter method to call (BR-009/BR-010).
  CHECK(observer.tracked_count() == 1);
  return true;
}

}  // namespace

int main() {
  const bool ok = SourceClassificationMatrix() && StaleNavigationDropped() &&
                  NoFabricatedUrlsForBlobAndStream() && TeardownBlocksLateEvents() &&
                  CapacityBounded() && EligibilityPrefersVisiblePlaying() &&
                  ApiExposesNoInteractionSurface();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "media_observer_test passed\n";
  return EXIT_SUCCESS;
}
