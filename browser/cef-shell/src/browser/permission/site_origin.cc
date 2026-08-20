#include "browser/permission/site_origin.h"

#include <cctype>
#include <string>

namespace crayon::browser::cef_shell::permission {
namespace {

// Checks whether |ch| is an unreserved character or one of the sub-delims
// that may appear in a registered name without percent-encoding.
bool IsValidHostChar(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' ||
         ch == '.' || ch == '_';
}

// Returns the default port for a scheme, or 0 if unknown.
std::uint16_t DefaultPortForScheme(std::string_view scheme) {
  if (scheme == "http") {
    return 80;
  }
  if (scheme == "https") {
    return 443;
  }
  return 0;
}

// Tries to read a decimal port from |view|.  Returns true and writes the
// value to |out| only when the entire view is a valid port in [1, 65535].
bool ParsePort(std::string_view view, std::uint16_t& out) {
  if (view.empty() || view.size() > 5) {
    return false;
  }
  unsigned int value = 0;
  for (char ch : view) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10 + static_cast<unsigned int>(ch - '0');
    if (value > 65535U) {
      return false;
    }
  }
  if (value == 0) {
    return false;
  }
  out = static_cast<std::uint16_t>(value);
  return true;
}

}  // namespace

std::optional<std::string> ExtractSiteOrigin(std::string_view url) {
  // Locate scheme.
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos || scheme_end == 0) {
    return std::nullopt;
  }

  std::string scheme;
  scheme.reserve(scheme_end);
  for (std::size_t i = 0; i < scheme_end; ++i) {
    const char ch = static_cast<char>(std::tolower(
        static_cast<unsigned char>(url[i])));
    // Scheme must start with alpha and contain only [a-z0-9+.-].
    if (i == 0 ? !std::isalpha(static_cast<unsigned char>(ch))
               : !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '+' ||
                   ch == '-' || ch == '.')) {
      return std::nullopt;
    }
    scheme.push_back(ch);
  }

  const std::uint16_t default_port = DefaultPortForScheme(scheme);
  if (default_port == 0) {
    // Only HTTP(S) origins are supported for permission scoping.
    return std::nullopt;
  }

  std::size_t pos = scheme_end + 3;  // skip "://"

  // Optional userinfo is rejected (contains '@').
  const std::size_t at = url.find('@', pos);
  const std::size_t path_start = url.find('/', pos);
  if (at != std::string_view::npos &&
      (path_start == std::string_view::npos || at < path_start)) {
    return std::nullopt;
  }

  // Extract host and optional port.
  const std::size_t authority_end =
      (path_start != std::string_view::npos) ? path_start : url.size();
  const std::size_t port_colon = url.rfind(':', authority_end);
  const bool has_port =
      port_colon != std::string_view::npos && port_colon > pos;

  std::string host;
  std::uint16_t port = default_port;

  if (has_port) {
    if (!ParsePort(url.substr(port_colon + 1, authority_end - port_colon - 1),
                   port)) {
      return std::nullopt;
    }
    host = std::string(url.substr(pos, port_colon - pos));
  } else {
    host = std::string(url.substr(pos, authority_end - pos));
  }

  if (host.empty()) {
    return std::nullopt;
  }

  // Validate host characters and normalise to lowercase.
  for (char& ch : host) {
    if (!IsValidHostChar(ch)) {
      return std::nullopt;
    }
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  for (char ch : host) {
    if (!IsValidHostChar(ch)) {
      return std::nullopt;
    }
  }

  // Build origin string.
  std::string origin = scheme + "://" + host;
  if (port != default_port) {
    origin += ":" + std::to_string(port);
  }
  return origin;
}

}  // namespace crayon::browser::cef_shell::permission
