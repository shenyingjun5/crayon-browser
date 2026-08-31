#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "browser/network_observer/network_observer.h"
#include "include/cef_browser.h"
#include "include/cef_request.h"
#include "include/cef_resource_request_handler.h"

namespace crayon::browser::cef_shell::observation {

struct CefNetworkResourceFact {
  int browser_id = 0;
  std::uint64_t navigation_id = 0;
  ::crayon::cef_shell::network::NetworkObservation observation;
  std::string present_header_name;
  std::uint64_t observed_at_ms = 0;
};

using CefNetworkResourceCallback = std::function<void(CefNetworkResourceFact)>;

// Creates a passive CEF resource observer. It never changes, redirects,
// filters, retries, or reads response bodies; request header values are not
// copied into the fact DTO.
CefRefPtr<CefResourceRequestHandler> CreateNetworkResourceObserver(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefRequest> request,
    std::uint64_t navigation_id, CefNetworkResourceCallback callback,
    CefRefPtr<CefBaseRefCounted> callback_owner);

}  // namespace crayon::browser::cef_shell::observation
