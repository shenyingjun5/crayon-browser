#include "browser/permission/site_origin.h"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>

using crayon::browser::cef_shell::permission::ExtractSiteOrigin;

namespace {

bool TestPassed = true;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << std::endl;
    TestPassed = false;
  }
}

void CheckEqual(const std::optional<std::string>& actual,
                const std::optional<std::string>& expected,
                const char* description) {
  if (actual != expected) {
    std::cerr << "FAIL: " << description << std::endl;
    std::cerr << "  expected: "
              << (expected.has_value() ? expected.value() : "<nullopt>")
              << std::endl;
    std::cerr << "  actual:   "
              << (actual.has_value() ? actual.value() : "<nullopt>")
              << std::endl;
    TestPassed = false;
  }
}

}  // namespace

int main() {
  // Basic HTTP/HTTPS origins.
  CheckEqual(ExtractSiteOrigin("http://example.com/path"),
             std::optional<std::string>("http://example.com"),
             "http origin with path");
  CheckEqual(ExtractSiteOrigin("https://example.com/path?query=1"),
             std::optional<std::string>("https://example.com"),
             "https origin with query");

  // Non-default ports are preserved.
  CheckEqual(ExtractSiteOrigin("http://example.com:8080/path"),
             std::optional<std::string>("http://example.com:8080"),
             "http with non-default port");
  CheckEqual(ExtractSiteOrigin("https://example.com:8443/"),
             std::optional<std::string>("https://example.com:8443"),
             "https with non-default port");

  // Default ports are omitted.
  CheckEqual(ExtractSiteOrigin("http://example.com:80/"),
             std::optional<std::string>("http://example.com"),
             "http default port omitted");
  CheckEqual(ExtractSiteOrigin("https://example.com:443/"),
             std::optional<std::string>("https://example.com"),
             "https default port omitted");

  // Scheme case normalisation.
  CheckEqual(ExtractSiteOrigin("HTTP://Example.COM/path"),
             std::optional<std::string>("http://example.com"),
             "uppercase scheme normalised");
  CheckEqual(ExtractSiteOrigin("HTTPS://EXAMPLE.COM/"),
             std::optional<std::string>("https://example.com"),
             "uppercase https scheme normalised");

  // Sub-domains.
  CheckEqual(ExtractSiteOrigin("https://sub.example.com/page"),
             std::optional<std::string>("https://sub.example.com"),
             "sub-domain origin");

  // Rejected: non-http(s) schemes.
  Check(!ExtractSiteOrigin("ftp://example.com/file").has_value(),
        "ftp scheme rejected");
  Check(!ExtractSiteOrigin("file:///etc/passwd").has_value(),
        "file scheme rejected");
  Check(!ExtractSiteOrigin("javascript:alert(1)").has_value(),
        "javascript scheme rejected");
  Check(!ExtractSiteOrigin("data:text/html,hello").has_value(),
        "data scheme rejected");

  // Rejected: userinfo.
  Check(!ExtractSiteOrigin("http://user:pass@example.com/").has_value(),
        "userinfo rejected");

  // Rejected: missing host.
  Check(!ExtractSiteOrigin("http:///path").has_value(), "missing host rejected");

  // Rejected: malformed URLs.
  Check(!ExtractSiteOrigin("not-a-url").has_value(),
        "bare string rejected");
  Check(!ExtractSiteOrigin("http://").has_value(), "http:// alone rejected");
  Check(!ExtractSiteOrigin("").has_value(), "empty string rejected");
  Check(!ExtractSiteOrigin("https://exam ple.com/").has_value(),
        "space in host rejected");

  // Origin with hyphen and underscore.
  CheckEqual(ExtractSiteOrigin("https://my-site_1.example.com/"),
             std::optional<std::string>("https://my-site_1.example.com"),
             "host with hyphen and underscore");

  // IPv4 literal host.
  CheckEqual(ExtractSiteOrigin("http://192.168.1.1:8080/path"),
             std::optional<std::string>("http://192.168.1.1:8080"),
             "IPv4 with port");

  if (TestPassed) {
    std::cout << "ALL TESTS PASSED" << std::endl;
    return 0;
  }
  return 1;
}
