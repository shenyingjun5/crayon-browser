#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_SITE_ORIGIN_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_SITE_ORIGIN_H_

#include <optional>
#include <string>
#include <string_view>

namespace crayon::browser::cef_shell::permission {

// Extracts a site-origin string from a URL for permission scoping.
//
// Returns "scheme://host:port" where port is omitted when it matches the
// scheme default (80 for http, 443 for https).  Returns std::nullopt for
// non-HTTP(S) URLs, malformed input, or URLs that do not contain a host.
//
// This is a lightweight parser used only for permission origin matching;
// it does not replace a full URL parser for navigation or security checks.
std::optional<std::string> ExtractSiteOrigin(std::string_view url);

}  // namespace crayon::browser::cef_shell::permission

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_PERMISSION_SITE_ORIGIN_H_
