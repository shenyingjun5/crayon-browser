#include "browser/network_observer/cef_network_observer_adapter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::observation {
namespace {

using ::crayon::cef_shell::network::NetworkObservation;
using ::crayon::cef_shell::network::ResourceKind;

std::string Lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

ResourceKind ClassifyResource(CefRefPtr<CefRequest> request) {
  if (!request) return ResourceKind::kOther;
  switch (request->GetResourceType()) {
    case RT_MAIN_FRAME:
    case RT_SUB_FRAME:
      return ResourceKind::kDocument;
    case RT_MEDIA:
      return ResourceKind::kMedia;
    default:
      break;
  }
  const std::string url = Lower(request->GetURL().ToString());
  if (url.find(".m3u8") != std::string::npos ||
      url.find(".mpd") != std::string::npos) {
    return ResourceKind::kManifest;
  }
  if (url.find(".m4s") != std::string::npos ||
      url.find(".ts") != std::string::npos ||
      url.find(".aac") != std::string::npos) {
    return ResourceKind::kSegment;
  }
  return ResourceKind::kOther;
}

std::string PresentHeaderName(CefRefPtr<CefRequest> request) {
  if (!request) return {};
  CefRequest::HeaderMap headers;
  request->GetHeaderMap(headers);
  bool authorization = false;
  bool range = false;
  bool referer = false;
  bool user_agent = false;
  for (const auto& header : headers) {
    const std::string name = Lower(header.first.ToString());
    if (name == "cookie" || name == "set-cookie") return "cookie";
    authorization = authorization || name == "authorization";
    range = range || name == "range";
    referer = referer || name == "referer";
    user_agent = user_agent || name == "user-agent";
  }
  if (authorization) return "authorization";
  if (range) return "range";
  if (referer) return "referer";
  if (user_agent) return "user-agent";
  return {};
}

std::uint64_t MonotonicMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

class ResourceObserver final : public CefResourceRequestHandler {
 public:
  ResourceObserver(CefNetworkResourceFact fact,
                   CefNetworkResourceCallback callback,
                   CefRefPtr<CefBaseRefCounted> callback_owner)
      : fact_(std::move(fact)),
        callback_(std::move(callback)),
        callback_owner_(std::move(callback_owner)) {}

  void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefRequest> request,
                              CefRefPtr<CefResponse> response,
                              URLRequestStatus status,
                              int64_t received_content_length) override {
    CEF_REQUIRE_IO_THREAD();
    static_cast<void>(browser);
    static_cast<void>(frame);
    static_cast<void>(request);
    static_cast<void>(response);
    static_cast<void>(status);
    static_cast<void>(received_content_length);
    fact_.observed_at_ms = MonotonicMilliseconds();
    auto callback = callback_;
    auto owner = callback_owner_;
    auto fact = fact_;
    CefPostTask(TID_UI,
                CefCreateClosureTask(base::BindOnce(
                    [](CefNetworkResourceCallback callback,
                       CefRefPtr<CefBaseRefCounted> owner,
                       CefNetworkResourceFact fact) {
                      static_cast<void>(owner);
                      callback(std::move(fact));
                    },
                    std::move(callback), std::move(owner), std::move(fact))));
  }

 private:
  CefNetworkResourceFact fact_;
  CefNetworkResourceCallback callback_;
  CefRefPtr<CefBaseRefCounted> callback_owner_;

  IMPLEMENT_REFCOUNTING(ResourceObserver);
  DISALLOW_COPY_AND_ASSIGN(ResourceObserver);
};

}  // namespace

CefRefPtr<CefResourceRequestHandler> CreateNetworkResourceObserver(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefRequest> request,
    std::uint64_t navigation_id, CefNetworkResourceCallback callback,
    CefRefPtr<CefBaseRefCounted> callback_owner) {
  CEF_REQUIRE_IO_THREAD();
  if (!browser || !request || navigation_id == 0 || !callback ||
      !callback_owner) {
    return nullptr;
  }
  NetworkObservation observation;
  observation.navigation_id = navigation_id;
  observation.url = request->GetURL().ToString();
  observation.kind = ClassifyResource(request);
  // Body bytes are deliberately not measured or retained. Probe and relay
  // own content semantics in later slices; this adapter observes only the URL
  // and closed request metadata.
  observation.content_length = 0;
  return new ResourceObserver(
      CefNetworkResourceFact{browser->GetIdentifier(), navigation_id,
                             std::move(observation), PresentHeaderName(request),
                             0},
      std::move(callback), std::move(callback_owner));
}

}  // namespace crayon::browser::cef_shell::observation
