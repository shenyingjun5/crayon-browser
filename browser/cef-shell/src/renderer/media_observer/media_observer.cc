#include "renderer/media_observer/media_observer.h"

#include <algorithm>

namespace crayon::cef_shell::renderer {
namespace {

bool IsHttpUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool HasAsciiControl(const std::string& url) {
  return std::any_of(url.begin(), url.end(), [](char c) {
    return static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
  });
}

}  // namespace

MediaSourceKind ClassifySourceUrl(const std::string& url, std::string* normalized) {
  if (normalized != nullptr) {
    normalized->clear();
  }
  if (url.empty() || url.size() > kMaxSourceUrlLen || HasAsciiControl(url)) {
    return MediaSourceKind::kUnknown;
  }
  if (url.rfind("blob:", 0) == 0) {
    return MediaSourceKind::kBlobUrl;
  }
  if (url.rfind("mediastream:", 0) == 0 ||
      url.rfind("media-stream:", 0) == 0) {
    return MediaSourceKind::kMediaStream;
  }
  if (IsHttpUrl(url)) {
    if (normalized != nullptr) {
      *normalized = url;
    }
    return MediaSourceKind::kHttpUrl;
  }
  return MediaSourceKind::kUnknown;
}

void MediaObserver::AdvanceNavigation(std::uint64_t navigation_id) {
  navigation_id_ = navigation_id;
  // Observations from the previous navigation can no longer be
  // trusted for eligibility.
  elements_.clear();
}

ObserveResult MediaObserver::Observe(MediaObservation observation) {
  if (torn_down_) {
    return ObserveResult::kDroppedTeardown;
  }
  if (observation.navigation_id != navigation_id_) {
    return ObserveResult::kDroppedStaleNavigation;
  }
  std::string normalized;
  const MediaSourceKind kind = ClassifySourceUrl(observation.source_url, &normalized);
  if (kind != observation.source_kind) {
    // The caller must classify first; a mismatching tag is dropped
    // rather than silently reinterpreted.
    if (observation.source_kind == MediaSourceKind::kHttpUrl ||
        (observation.source_kind == MediaSourceKind::kBlobUrl &&
         kind != MediaSourceKind::kBlobUrl) ||
        (observation.source_kind == MediaSourceKind::kMediaStream &&
         kind != MediaSourceKind::kMediaStream)) {
      return ObserveResult::kDroppedInvalidUrl;
    }
  }
  if (observation.source_kind == MediaSourceKind::kHttpUrl && normalized.empty()) {
    return ObserveResult::kDroppedInvalidUrl;
  }
  // blob:/stream sources must never carry a fabricated URL.
  if (observation.source_kind != MediaSourceKind::kHttpUrl &&
      !observation.source_url.empty()) {
    return ObserveResult::kDroppedInvalidUrl;
  }
  observation.source_url = normalized;
  observation.frame_id = frame_id_;
  observation.visible_fraction =
      std::min(1.0, std::max(0.0, observation.visible_fraction));

  auto it = std::find_if(elements_.begin(), elements_.end(),
                         [&observation](const MediaObservation& existing) {
                           return existing.element_id == observation.element_id;
                         });
  if (it != elements_.end()) {
    *it = observation;
    return ObserveResult::kAccepted;
  }
  if (elements_.size() >= kMaxMediaElements) {
    return ObserveResult::kDroppedCapacity;
  }
  elements_.push_back(observation);
  return ObserveResult::kAccepted;
}

void MediaObserver::TearDown() {
  torn_down_ = true;
  elements_.clear();
}

std::optional<MediaObservation> MediaObserver::FindEligible(
    std::uint64_t navigation_id) const {
  if (torn_down_ || navigation_id != navigation_id_) {
    return std::nullopt;
  }
  // Playback + visibility precondition; the browser process re-checks
  // with trusted input before anything is authorized (CEF-10).
  const MediaObservation* best = nullptr;
  for (const MediaObservation& element : elements_) {
    if (element.playback != MediaPlaybackState::kPlaying ||
        element.visible_fraction <= 0.0) {
      continue;
    }
    if (best == nullptr ||
        element.visible_fraction > best->visible_fraction) {
      best = &element;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  return *best;
}

}  // namespace crayon::cef_shell::renderer
