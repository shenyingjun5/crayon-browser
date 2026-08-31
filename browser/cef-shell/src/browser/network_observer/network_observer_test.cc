// CEF-11 contract tests: closed URL/header field set, size/rate caps,
// sensitive headers rejected, EME association, blob without fabricated
// URLs.
#include "browser/network_observer/network_observer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using crayon::cef_shell::network::HeaderClass;
using crayon::cef_shell::network::kMaxObservations;
using crayon::cef_shell::network::kRateBurst;
using crayon::cef_shell::network::NetworkObservation;
using crayon::cef_shell::network::NetworkObserver;
using crayon::cef_shell::network::NetworkObserveResult;
using crayon::cef_shell::network::ResourceKind;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

NetworkObservation Media(const char* url, std::uint64_t nav) {
  NetworkObservation observation;
  observation.url = url;
  observation.navigation_id = nav;
  observation.kind = ResourceKind::kMedia;
  observation.content_length = 1'000;
  return observation;
}

bool UrlClassificationMatrix() {
  std::string normalized;
  bool is_blob = false;
  CHECK(NetworkObserver::ClassifyUrl("https://a.example/v.mp4", &normalized,
                                     &is_blob));
  CHECK(normalized == "https://a.example/v.mp4" && !is_blob);
  CHECK(NetworkObserver::ClassifyUrl("blob:https://a.example/x", &normalized,
                                     &is_blob));
  CHECK(normalized.empty() && is_blob);
  CHECK(!NetworkObserver::ClassifyUrl("javascript:alert(1)", &normalized,
                                      &is_blob));
  CHECK(!NetworkObserver::ClassifyUrl("", &normalized, &is_blob));
  CHECK(!NetworkObserver::ClassifyUrl(std::string(2049, 'a'), &normalized,
                                      &is_blob));
  CHECK(!NetworkObserver::ClassifyUrl(std::string("https://a\tx"), &normalized,
                                      &is_blob));
  return true;
}

bool HeaderAllowlistMatrix() {
  HeaderClass header_class = HeaderClass::kNone;
  CHECK(NetworkObserver::IsObservableHeader("referer", &header_class));
  CHECK(header_class == HeaderClass::kReferer);
  CHECK(NetworkObserver::IsObservableHeader("user-agent", &header_class));
  CHECK(header_class == HeaderClass::kUserAgent);
  CHECK(NetworkObserver::IsObservableHeader("range", &header_class));
  CHECK(NetworkObserver::IsObservableHeader("authorization", &header_class));
  CHECK(header_class == HeaderClass::kAuthorization);
  CHECK(!NetworkObserver::IsObservableHeader("cookie", &header_class));
  CHECK(!NetworkObserver::IsObservableHeader("set-cookie", &header_class));
  CHECK(!NetworkObserver::IsObservableHeader("x-custom", &header_class));
  CHECK(!NetworkObserver::IsObservableHeader("", &header_class));
  return true;
}

bool SensitiveHeaderRejectsObservation() {
  NetworkObserver observer;
  // A request carrying Cookie rejects the whole observation — no value
  // ever enters the DTO (BR-008).
  CHECK(observer.Observe(Media("https://a.example/v.mp4", 1), "cookie", 0) ==
        NetworkObserveResult::kDroppedSensitiveHeader);
  CHECK(observer.Observe(Media("https://a.example/v.mp4", 1), "x-secret", 0) ==
        NetworkObserveResult::kDroppedSensitiveHeader);
  CHECK(observer.retained_count() == 0);
  // Authorization is observable only as a closed class: the DTO keeps
  // no header value. Match the real CEF adapter: it provides only the
  // closed name and the observer derives the DTO class.
  NetworkObservation auth = Media("https://a.example/v.mp4", 1);
  CHECK(observer.Observe(auth, "authorization", 1) ==
        NetworkObserveResult::kAccepted);
  const auto drained = observer.Drain();
  CHECK(drained.size() == 1 &&
        drained[0].header_class == HeaderClass::kAuthorization);
  return true;
}

bool BlobNeverFabricated() {  // BR-012
  NetworkObserver observer;
  CHECK(observer.Observe(Media("blob:https://a.example/uuid", 1), "", 0) ==
        NetworkObserveResult::kAccepted);
  const auto drained = observer.Drain();
  CHECK(drained.size() == 1 && drained[0].url.empty());
  return true;
}

bool EmeAssociationUpgradesProtection() {  // BR-011
  NetworkObserver observer;
  observer.Observe(Media("https://a.example/clear.mp4", 7), "", 0);
  NetworkObservation doc;
  doc.url = "https://a.example/page.html";
  doc.navigation_id = 7;
  doc.kind = ResourceKind::kDocument;
  observer.Observe(doc, "", 1);
  observer.Observe(Media("https://a.example/other-nav.mp4", 8), "", 2);
  observer.AssociateEmeEncrypted(7);
  const auto drained = observer.Drain();
  CHECK(drained.size() == 3);
  for (const auto& observation : drained) {
    if (observation.navigation_id == 7) {
      // Media upgrades; the document on the same navigation does not.
      CHECK(observation.eme_encrypted ==
            (observation.kind != ResourceKind::kDocument));
    } else {
      CHECK(!observation.eme_encrypted);
    }
  }
  return true;
}

bool RateAndCapacityBounds() {
  NetworkObserver observer;
  // Token bucket: drain the burst inside one instant. Empty the bounded
  // retention store partway through so the independent capacity limit does
  // not mask the rate-limit assertion.
  for (std::uint32_t i = 0; i < kRateBurst; ++i) {
    if (observer.retained_count() == kMaxObservations) {
      static_cast<void>(observer.Drain());
    }
    CHECK(observer.Observe(Media("https://a.example/s.mp4", 1), "", 0) ==
          NetworkObserveResult::kAccepted);
  }
  CHECK(observer.Observe(Media("https://a.example/s.mp4", 1), "", 0) ==
        NetworkObserveResult::kDroppedRateLimited);
  // No boundary doubling: the next millisecond only refills a quarter
  // token, so a second instant burst is still shed (the CEF-11 review
  // follow-up this fixed window used to allow).
  CHECK(observer.Observe(Media("https://a.example/s.mp4", 1), "", 1) ==
        NetworkObserveResult::kDroppedRateLimited);
  // After enough elapsed time the bucket refills toward capacity.
  static_cast<void>(observer.Drain());
  CHECK(observer.Observe(Media("https://a.example/s.mp4", 1), "", 100'000) ==
        NetworkObserveResult::kAccepted);
  // Capacity still binds the retained store.
  std::size_t accepted = observer.retained_count();
  for (std::uint32_t i = 0; accepted < kMaxObservations; ++i) {
    const auto result = observer.Observe(Media("https://a.example/s.mp4", 1),
                                         "", 100'000 + i * 100'000ULL);
    if (result == NetworkObserveResult::kAccepted) {
      ++accepted;
    } else {
      CHECK(result == NetworkObserveResult::kDroppedCapacity);
      break;
    }
  }
  CHECK(observer.retained_count() == kMaxObservations);
  CHECK(observer.Observe(Media("https://a.example/s.mp4", 1), "", 10'000'000) ==
        NetworkObserveResult::kDroppedCapacity);
  // Oversize content_length metadata is dropped.
  NetworkObserver fresh;
  NetworkObservation huge = Media("https://a.example/big.bin", 1);
  huge.content_length = 1ULL << 40;
  CHECK(fresh.Observe(huge, "", 0) == NetworkObserveResult::kDroppedOversize);
  return true;
}

}  // namespace

int main() {
  const bool ok = UrlClassificationMatrix() && HeaderAllowlistMatrix() &&
                  SensitiveHeaderRejectsObservation() &&
                  BlobNeverFabricated() && EmeAssociationUpgradesProtection() &&
                  RateAndCapacityBounds();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "network_observer_test passed\n";
  return EXIT_SUCCESS;
}
