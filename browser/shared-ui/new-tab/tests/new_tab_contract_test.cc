#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/browser_new_tab/new_tab_page.h"

namespace {

using crayon::browser_new_tab::BuildNewTabPageModel;
using crayon::browser_new_tab::ClassifyNewTabRequest;
using crayon::browser_new_tab::NewTabConfigStatus;
using crayon::browser_new_tab::NewTabPageStrings;
using crayon::browser_new_tab::NewTabProfileMode;
using crayon::browser_new_tab::NewTabRequestParts;
using crayon::browser_new_tab::NewTabResourceKind;
using crayon::browser_new_tab::RenderNewTabDocument;
using crayon::browser_new_tab::RenderNewTabStylesheet;
using crayon::browser_new_tab::ShortcutConfig;
using crayon::browser_new_tab::ShortcutEntry;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

NewTabPageStrings TestStrings() {
  return NewTabPageStrings{
      "en-US",
      "Crayon Browser",
      "A quiet place to begin",
      "Private browsing",
      "Use the address bar to search or navigate.",
      "This page does not show cross-session suggestions.",
      "Press Ctrl+L to search or enter an address",
      "Pinned shortcuts",
      "No pinned shortcuts yet",
      "Pinned shortcuts could not be loaded",
  };
}

ShortcutConfig ValidConfig() {
  ShortcutConfig config;
  config.schema_version = 1;
  config.entries = {
      ShortcutEntry{"docs", "Docs & Notes", "https://docs.example.test/"},
      ShortcutEntry{"media", "Media", "http://media.example.test/path?q=1&v=2"},
  };
  return config;
}

bool DefaultPageHasNoPublicOrActiveReferences() {
  const auto model =
      BuildNewTabPageModel(NewTabProfileMode::kRegular, ShortcutConfig{});
  CHECK(model.config_status == NewTabConfigStatus::kAccepted);
  CHECK(model.shortcuts.empty());
  const std::string document = RenderNewTabDocument(model, TestStrings());
  CHECK(document.find("crayon://newtab/styles.css") != std::string::npos);
  CHECK(document.find("http://") == std::string::npos);
  CHECK(document.find("https://") == std::string::npos);
  CHECK(document.find("<script") == std::string::npos);
  CHECK(document.find("<form") == std::string::npos);
  CHECK(document.find("<iframe") == std::string::npos);
  CHECK(document.find("<object") == std::string::npos);
  CHECK(document.find("<img") == std::string::npos);
  return true;
}

bool RegularPageEscapesValidatedShortcuts() {
  ShortcutConfig config = ValidConfig();
  config.entries[0].title = u8"\u6587\u6863 <unsafe> & notes";
  const auto model = BuildNewTabPageModel(NewTabProfileMode::kRegular, config);
  CHECK(model.config_status == NewTabConfigStatus::kAccepted);
  CHECK(model.shortcuts.size() == 2);
  const std::string document = RenderNewTabDocument(model, TestStrings());
  CHECK(document.find(u8"\u6587\u6863 &lt;unsafe&gt; &amp; notes") !=
        std::string::npos);
  CHECK(document.find("Docs <unsafe>") == std::string::npos);
  CHECK(document.find(u8">\u6587</span>") != std::string::npos);
  CHECK(document.find("q=1&amp;v=2") != std::string::npos);
  CHECK(document.find("rel=\"noreferrer\"") != std::string::npos);
  return true;
}

bool CorruptShortcutConfigurationsFailClosed() {
  ShortcutConfig unsupported = ValidConfig();
  unsupported.schema_version = 2;
  auto model = BuildNewTabPageModel(NewTabProfileMode::kRegular, unsupported);
  CHECK(model.config_status == NewTabConfigStatus::kUnsupportedVersion);
  CHECK(model.shortcuts.empty());

  ShortcutConfig duplicate = ValidConfig();
  duplicate.entries[1].id = duplicate.entries[0].id;
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, duplicate);
  CHECK(model.config_status == NewTabConfigStatus::kDuplicateId);
  CHECK(model.shortcuts.empty());

  ShortcutConfig invalid = ValidConfig();
  invalid.entries[0].url = "javascript:alert(1)";
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, invalid);
  CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);
  CHECK(model.shortcuts.empty());

  invalid = ValidConfig();
  invalid.entries[0].title.clear();
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, invalid);
  CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);

  invalid = ValidConfig();
  invalid.entries[0].title = "line\nbreak";
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, invalid);
  CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);

  invalid = ValidConfig();
  invalid.entries[0].title = std::string("bad-") + '\xC0';
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, invalid);
  CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);

  invalid = ValidConfig();
  invalid.entries[0].id = "bad/id";
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, invalid);
  CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);

  for (const char* bad_url :
       {"https://", "https://example.test:0/", "https://example.test\\path",
        "https://./", "https://-bad.example/", "data:text/plain,unsafe"}) {
    invalid = ValidConfig();
    invalid.entries[0].url = bad_url;
    model = BuildNewTabPageModel(NewTabProfileMode::kRegular, invalid);
    CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);
  }

  ShortcutConfig credentials = ValidConfig();
  credentials.entries[0].url = "https://user:pass@example.test/";
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, credentials);
  CHECK(model.config_status == NewTabConfigStatus::kInvalidEntry);
  CHECK(model.shortcuts.empty());

  ShortcutConfig excessive;
  excessive.schema_version = 1;
  for (std::uint32_t index = 0;
       index < crayon::browser_new_tab::kMaximumShortcutCount + 1; ++index) {
    excessive.entries.push_back(
        ShortcutEntry{"item-" + std::to_string(index), "Item",
                      "https://example.test/" + std::to_string(index)});
  }
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, excessive);
  CHECK(model.config_status == NewTabConfigStatus::kTooManyEntries);
  CHECK(model.shortcuts.empty());

  excessive.entries.pop_back();
  model = BuildNewTabPageModel(NewTabProfileMode::kRegular, excessive);
  CHECK(model.config_status == NewTabConfigStatus::kAccepted);
  CHECK(model.shortcuts.size() ==
        crayon::browser_new_tab::kMaximumShortcutCount);
  return true;
}

bool IncognitoPageNeverRendersCrossSessionShortcuts() {
  const auto model =
      BuildNewTabPageModel(NewTabProfileMode::kIncognito, ValidConfig());
  CHECK(model.config_status == NewTabConfigStatus::kAccepted);
  CHECK(model.shortcuts.empty());
  const std::string document = RenderNewTabDocument(model, TestStrings());
  CHECK(document.find("data-profile-mode=\"incognito\"") != std::string::npos);
  CHECK(document.find("Private browsing") != std::string::npos);
  CHECK(document.find("Docs &amp; Notes") == std::string::npos);
  CHECK(document.find("docs.example.test") == std::string::npos);
  CHECK(document.find("Pinned shortcuts") == std::string::npos);

  ShortcutConfig corrupt = ValidConfig();
  corrupt.entries[0].url = "javascript:alert(1)";
  const auto corrupt_model =
      BuildNewTabPageModel(NewTabProfileMode::kIncognito, corrupt);
  CHECK(corrupt_model.config_status == NewTabConfigStatus::kInvalidEntry);
  CHECK(corrupt_model.shortcuts.empty());
  return true;
}

NewTabRequestParts BaseRequest() {
  return NewTabRequestParts{"GET", "crayon", "newtab", "/",
                            false, false,    false,    false};
}

bool RequestRouterIsExactAndFailClosed() {
  auto request = BaseRequest();
  auto route = ClassifyNewTabRequest(request);
  CHECK(route.kind == NewTabResourceKind::kDocument);
  CHECK(route.status_code == 200);
  CHECK(route.include_body);

  request.method = "HEAD";
  request.path = "/index.html";
  route = ClassifyNewTabRequest(request);
  CHECK(route.kind == NewTabResourceKind::kDocument);
  CHECK(route.status_code == 200);
  CHECK(!route.include_body);

  request.method = "GET";
  request.path = "/styles.css";
  route = ClassifyNewTabRequest(request);
  CHECK(route.kind == NewTabResourceKind::kStylesheet);
  CHECK(route.status_code == 200);

  request.path = "/missing";
  route = ClassifyNewTabRequest(request);
  CHECK(route.kind == NewTabResourceKind::kNotFound);
  CHECK(route.status_code == 404);

  request = BaseRequest();
  request.method = "POST";
  route = ClassifyNewTabRequest(request);
  CHECK(route.kind == NewTabResourceKind::kMethodNotAllowed);
  CHECK(route.status_code == 405);

  for (int variant = 0; variant < 6; ++variant) {
    request = BaseRequest();
    if (variant == 0) request.scheme = "https";
    if (variant == 1) request.host = "other";
    if (variant == 2) request.has_credentials = true;
    if (variant == 3) request.has_port = true;
    if (variant == 4) request.has_query = true;
    if (variant == 5) request.has_fragment = true;
    route = ClassifyNewTabRequest(request);
    CHECK(route.kind == NewTabResourceKind::kRejected);
    CHECK(route.status_code == 0);
    CHECK(!route.include_body);
  }
  return true;
}

bool StylesheetHasNoExternalFetchSurface() {
  const std::string stylesheet = RenderNewTabStylesheet();
  CHECK(stylesheet.find("color-scheme: light dark") != std::string::npos);
  CHECK(stylesheet.find("url(") == std::string::npos);
  CHECK(stylesheet.find("@import") == std::string::npos);
  CHECK(stylesheet.find("http://") == std::string::npos);
  CHECK(stylesheet.find("https://") == std::string::npos);
  return true;
}

}  // namespace

int main() {
  if (!DefaultPageHasNoPublicOrActiveReferences() ||
      !RegularPageEscapesValidatedShortcuts() ||
      !CorruptShortcutConfigurationsFailClosed() ||
      !IncognitoPageNeverRendersCrossSessionShortcuts() ||
      !RequestRouterIsExactAndFailClosed() ||
      !StylesheetHasNoExternalFetchSurface()) {
    return 1;
  }
  return 0;
}
