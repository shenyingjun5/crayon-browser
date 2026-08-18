#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_omnibox/omnibox_parser.h"

namespace {

using crayon::browser_omnibox::IsValid;
using crayon::browser_omnibox::OmniboxInput;
using crayon::browser_omnibox::OmniboxParseResult;
using crayon::browser_omnibox::ParseOmniboxInput;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool EmptyInputIsSearchQuery() {
  const auto input = OmniboxInput::TryCreate("");
  CHECK(input.has_value());
  CHECK(ParseOmniboxInput(*input) == OmniboxParseResult::kSearchQuery);
  return true;
}

bool HttpUrlIsValid() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("http://example.test")) ==
        OmniboxParseResult::kValidUrl);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("https://example.test/path")) ==
        OmniboxParseResult::kValidUrl);
  return true;
}

bool HttpsUrlWithPortAndQueryIsValid() {
  CHECK(ParseOmniboxInput(
            *OmniboxInput::TryCreate("https://example.test:8080/path?q=1")) ==
        OmniboxParseResult::kValidUrl);
  return true;
}

bool FileSchemeIsValid() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("file:///tmp/test.html")) ==
        OmniboxParseResult::kValidUrl);
  return true;
}

bool CrayonSchemeIsValid() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("crayon://newtab/")) ==
        OmniboxParseResult::kValidUrl);
  return true;
}

bool DangerousSchemesAreBlocked() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("javascript:alert(1)")) ==
        OmniboxParseResult::kDangerous);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("data:text/html,<script>")) ==
        OmniboxParseResult::kDangerous);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("vbscript:msgbox(1)")) ==
        OmniboxParseResult::kDangerous);
  return true;
}

bool Ipv4LiteralIsValid() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("192.168.1.1")) ==
        OmniboxParseResult::kValidUrl);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("10.0.0.1")) ==
        OmniboxParseResult::kValidUrl);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("255.255.255.255")) ==
        OmniboxParseResult::kValidUrl);
  return true;
}

bool BadIpv4IsSearchQuery() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("256.1.1.1")) ==
        OmniboxParseResult::kSearchQuery);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("192.168.1")) ==
        OmniboxParseResult::kSearchQuery);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("192.168.01.1")) ==
        OmniboxParseResult::kSearchQuery);
  return true;
}

bool DomainLikeIsValid() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("example.test")) ==
        OmniboxParseResult::kValidUrl);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("www.example.com")) ==
        OmniboxParseResult::kValidUrl);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("sub.domain.co.uk")) ==
        OmniboxParseResult::kValidUrl);
  return true;
}

bool TldTooShortOrNonAlphaIsSearchQuery() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("example.c")) ==
        OmniboxParseResult::kSearchQuery);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("example.123")) ==
        OmniboxParseResult::kSearchQuery);
  return true;
}

bool PlainSearchQuery() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("hello world")) ==
        OmniboxParseResult::kSearchQuery);
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate(" how to browser")) ==
        OmniboxParseResult::kSearchQuery);
  return true;
}

bool ChineseInputIsSearchQuery() {
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("蜡笔浏览器")) ==
        OmniboxParseResult::kSearchQuery);
  return true;
}

bool OversizedInputRejectedAtBoundary() {
  const std::string huge(2049, 'a');
  CHECK(!OmniboxInput::TryCreate(huge).has_value());
  return true;
}

bool ExactlyMaxLengthAccepted() {
  const std::string exact(2048, 'a');
  CHECK(OmniboxInput::TryCreate(exact).has_value());
  return true;
}

bool DangerousSchemeWithSpacesIsSearchQuery() {
  // "javascript:" with a space before the colon is not a scheme
  CHECK(ParseOmniboxInput(*OmniboxInput::TryCreate("javascript :alert(1)")) ==
        OmniboxParseResult::kSearchQuery);
  return true;
}

bool IsValidCoversAllResults() {
  CHECK(IsValid(OmniboxParseResult::kValidUrl));
  CHECK(IsValid(OmniboxParseResult::kSearchQuery));
  CHECK(IsValid(OmniboxParseResult::kDangerous));
  return true;
}

}  // namespace

int main() {
  if (!EmptyInputIsSearchQuery() ||
      !HttpUrlIsValid() ||
      !HttpsUrlWithPortAndQueryIsValid() ||
      !FileSchemeIsValid() ||
      !CrayonSchemeIsValid() ||
      !DangerousSchemesAreBlocked() ||
      !Ipv4LiteralIsValid() ||
      !BadIpv4IsSearchQuery() ||
      !DomainLikeIsValid() ||
      !TldTooShortOrNonAlphaIsSearchQuery() ||
      !PlainSearchQuery() ||
      !ChineseInputIsSearchQuery() ||
      !OversizedInputRejectedAtBoundary() ||
      !ExactlyMaxLengthAccepted() ||
      !DangerousSchemeWithSpacesIsSearchQuery() ||
      !IsValidCoversAllResults()) {
    return 1;
  }
  return 0;
}
