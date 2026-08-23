#include "browser/network_observer/network_observer.h"

#include <algorithm>

namespace crayon::cef_shell::network {
namespace {

bool HasAsciiControl(const std::string& url) {
  return std::any_of(url.begin(), url.end(), [](char c) {
    return static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
  });
}

}  // namespace

bool NetworkObserver::ClassifyUrl(const std::string& url, std::string* normalized,
                                  bool* is_blob) {
  if (normalized != nullptr) {
    normalized->clear();
  }
  if (is_blob != nullptr) {
    *is_blob = false;
  }
  if (url.empty() || url.size() > kMaxUrlLen || HasAsciiControl(url)) {
    return false;
  }
  if (url.rfind("blob:", 0) == 0) {
    // BR-012: blob sources have no castable URL; observed as blob with
    // no normalized URL, never fabricated.
    if (is_blob != nullptr) {
      *is_blob = true;
    }
    return true;
  }
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
    return false;
  }
  if (normalized != nullptr) {
    *normalized = url;
  }
  return true;
}

bool NetworkObserver::IsObservableHeader(const std::string& lowercase_name,
                                         HeaderClass* out_class) {
  if (lowercase_name == "referer") {
    if (out_class != nullptr) *out_class = HeaderClass::kReferer;
    return true;
  }
  if (lowercase_name == "user-agent") {
    if (out_class != nullptr) *out_class = HeaderClass::kUserAgent;
    return true;
  }
  if (lowercase_name == "range") {
    if (out_class != nullptr) *out_class = HeaderClass::kRange;
    return true;
  }
  if (lowercase_name == "authorization") {
    // Observable only as a closed class flag; the value never enters
    // the DTO.
    if (out_class != nullptr) *out_class = HeaderClass::kAuthorization;
    return true;
  }
  return false;
}

NetworkObserveResult NetworkObserver::Observe(NetworkObservation observation,
                                              const std::string& present_header_name,
                                              std::uint64_t now_ms) {
  std::string normalized;
  bool is_blob = false;
  if (!ClassifyUrl(observation.url, &normalized, &is_blob)) {
    return NetworkObserveResult::kDroppedInvalidUrl;
  }
  // Sensitive headers outside the closed observable set (cookie, and
  // anything unclassified) reject the observation instead of leaking.
  if (!present_header_name.empty() &&
      !IsObservableHeader(present_header_name, nullptr)) {
    return NetworkObserveResult::kDroppedSensitiveHeader;
  }
  if (observation.content_length > kMaxUrlLen * 1024) {
    return NetworkObserveResult::kDroppedOversize;
  }
  // Token-bucket style fixed window with the injected clock.
  if (now_ms - window_start_ms_ >= kRateWindowMs) {
    window_start_ms_ = now_ms;
    window_used_ = 0;
  }
  if (window_used_ >= kRateWindowBudget) {
    return NetworkObserveResult::kDroppedRateLimited;
  }
  ++window_used_;

  observation.url = normalized;  // blob keeps an empty URL
  if (observations_.size() >= kMaxObservations) {
    return NetworkObserveResult::kDroppedCapacity;
  }
  observations_.push_back(observation);
  return NetworkObserveResult::kAccepted;
}

void NetworkObserver::AssociateEmeEncrypted(std::uint64_t navigation_id) {
  for (NetworkObservation& observation : observations_) {
    if (observation.navigation_id == navigation_id &&
        (observation.kind == ResourceKind::kMedia ||
         observation.kind == ResourceKind::kManifest ||
         observation.kind == ResourceKind::kSegment)) {
      observation.eme_encrypted = true;  // BR-011 protection upgrade
    }
  }
}

std::vector<NetworkObservation> NetworkObserver::Drain() {
  std::vector<NetworkObservation> drained = observations_;
  observations_.clear();
  return drained;
}

}  // namespace crayon::cef_shell::network
