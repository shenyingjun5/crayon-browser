#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/new_tab/localized_strings.h"
#include "crayon/new_tab/new_tab.h"

namespace new_tab = crayon::browser::new_tab;

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

new_tab::NewTabStrings TestStrings() {
  return {"en",        "Crayon Browser",   "Search or enter address",
          "Shortcuts", "Private browsing", "History and shortcuts stay hidden.",
          "Cast"};
}

void StandardModelFiltersCandidatesDeterministically() {
  std::vector<new_tab::ShortcutCandidate> candidates = {
      {"Docs", "https://docs.example.test/start"},
      {"duplicate", "https://docs.example.test/start"},
      {"query", "https://example.test/?secret=1"},
      {"userinfo", "https://user@example.test/"},
      {"script", "javascript:alert(1)"},
      {"Local", "http://localhost/welcome"},
  };
  const new_tab::NewTabModel model =
      new_tab::BuildNewTabModel(new_tab::ProfileMode::kStandard, candidates);
  Expect(model.shortcuts.size() == 2, "invalid and duplicate shortcuts drop");
  Expect(model.shortcuts[0].title == "Docs", "input order remains stable");
  Expect(model.shortcuts[1].title == "Local", "http shortcut is accepted");
  Expect(model.show_shortcuts, "standard profile exposes valid shortcuts");
  Expect(model.show_cast_entry, "standard profile exposes inert cast entry");
}

void PrivateModelNeverExposesInputs() {
  const new_tab::NewTabModel model = new_tab::BuildNewTabModel(
      new_tab::ProfileMode::kPrivate,
      {{"Private input", "https://private.example.test/"}});
  Expect(model.shortcuts.empty(), "private profile drops shortcut inputs");
  Expect(!model.show_shortcuts, "private profile hides shortcut surface");
  Expect(model.show_cast_entry,
         "private profile retains the inert first-class cast entry");
  const auto resource = new_tab::BuildNewTabResource(
      new_tab::NewTabRequestKind::kGet, model, TestStrings());
  Expect(resource.has_value(), "private resource builds");
  Expect(resource->body.find("private.example.test") == std::string::npos,
         "private resource cannot expose discarded shortcut inputs");
  Expect(resource->body.find("Private browsing") != std::string::npos,
         "private resource renders its dedicated state");
  Expect(resource->body.find(">Cast</button>") != std::string::npos,
         "private resource keeps the disabled cast entry visible");
}

void ShortcutCapacityAndCorruptConfigAreBounded() {
  std::vector<new_tab::ShortcutCandidate> candidates;
  candidates.push_back({"", "https://empty-title.example.test/"});
  candidates.push_back(
      {std::string(new_tab::kMaximumShortcutTitleBytes + 1, 'x'),
       "https://long-title.example.test/"});
  candidates.push_back({"control\n", "https://control.example.test/"});
  candidates.push_back({" invalid", "https://space.example.test/"});
  candidates.push_back({"invalid host", "https://bad..example.test/"});
  candidates.push_back({"invalid label", "https://-bad.example.test/"});
  for (std::size_t index = 0; index < new_tab::kMaximumPinnedShortcuts + 4;
       ++index) {
    candidates.push_back(
        {"valid", "https://example.test/path/" + std::to_string(index)});
  }
  const new_tab::NewTabModel model =
      new_tab::BuildNewTabModel(new_tab::ProfileMode::kStandard, candidates);
  Expect(model.shortcuts.size() == new_tab::kMaximumPinnedShortcuts,
         "shortcut count is bounded");
}

void RequestAllowlistIsExact() {
  Expect(new_tab::ValidateNewTabRequest("GET", "crayon://newtab/") ==
             new_tab::NewTabRequestKind::kGet,
         "exact GET is accepted");
  Expect(new_tab::ValidateNewTabRequest("HEAD", "crayon://newtab/") ==
             new_tab::NewTabRequestKind::kHead,
         "exact HEAD is accepted");
  for (const auto& request : std::vector<std::pair<std::string, std::string>>{
           {"POST", "crayon://newtab/"},
           {"GET", "crayon://newtab"},
           {"GET", "crayon://newtab/path"},
           {"GET", "crayon://newtab/?query"},
           {"GET", "crayon://newtab/#fragment"},
           {"GET", "crayon://user@newtab/"},
           {"GET", "crayon://newtab:80/"},
           {"GET", "crayon://other/"},
           {"GET", "https://newtab/"},
       }) {
    Expect(new_tab::ValidateNewTabRequest(request.first, request.second) ==
               new_tab::NewTabRequestKind::kReject,
           "non-exact request is rejected");
  }
}

void ResourceIsEscapedDeterministicAndNetworkClosed() {
  const new_tab::NewTabModel model =
      new_tab::BuildNewTabModel(new_tab::ProfileMode::kStandard,
                                {{"<script>alert(&quot;x&quot;)</script>",
                                  "https://safe.example.test/path"}});
  const auto first = new_tab::BuildNewTabResource(
      new_tab::NewTabRequestKind::kGet, model, TestStrings());
  const auto second = new_tab::BuildNewTabResource(
      new_tab::NewTabRequestKind::kGet, model, TestStrings());
  Expect(first.has_value() && second.has_value(), "valid resource builds");
  Expect(first->body == second->body, "resource output is deterministic");
  Expect(first->body.find("<script>alert") == std::string::npos,
         "dynamic title cannot create script markup");
  Expect(first->body.find("&lt;script&gt;") != std::string::npos,
         "dynamic title is HTML escaped");
  Expect(first->content_security_policy.find("default-src 'none'") !=
             std::string::npos,
         "CSP denies network by default");
  Expect(first->cache_control == "no-store", "resource is never cached");
  Expect(first->mime_type == "text/html", "resource MIME type is fixed");
  Expect(first->charset == "utf-8", "resource charset is fixed");
  Expect(first->body.size() <= new_tab::kMaximumRenderedPageBytes,
         "rendered output is bounded");

  const auto head = new_tab::BuildNewTabResource(
      new_tab::NewTabRequestKind::kHead, model, TestStrings());
  Expect(head.has_value() && head->body.empty(), "HEAD has no response body");
  Expect(!new_tab::BuildNewTabResource(new_tab::NewTabRequestKind::kReject,
                                       model, TestStrings())
              .has_value(),
         "rejected request has no resource");
}

void InvalidStringsFailClosed() {
  new_tab::NewTabStrings strings = TestStrings();
  strings.page_title.clear();
  const auto resource = new_tab::BuildNewTabResource(
      new_tab::NewTabRequestKind::kGet,
      new_tab::BuildNewTabModel(new_tab::ProfileMode::kStandard, {}), strings);
  Expect(!resource.has_value(), "corrupt locale config fails closed");

  strings = TestStrings();
  strings.language_tag = "en\"><script";
  Expect(!new_tab::BuildNewTabResource(
              new_tab::NewTabRequestKind::kGet,
              new_tab::BuildNewTabModel(new_tab::ProfileMode::kStandard, {}),
              strings)
              .has_value(),
         "invalid language tag fails closed");
}

void LocalizedStringsComeFromValidatedResources() {
  const auto english = new_tab::EnglishNewTabStrings();
  const auto chinese = new_tab::ChineseNewTabStrings();
  Expect(english.page_title == "Crayon Browser",
         "English locale is compiled from locale JSON");
  Expect(chinese.page_title == "蜡笔浏览器",
         "Chinese locale is compiled from locale JSON");
  Expect(new_tab::BuildNewTabResource(
             new_tab::NewTabRequestKind::kGet,
             new_tab::BuildNewTabModel(new_tab::ProfileMode::kStandard, {}),
             chinese)
             .has_value(),
         "localized resource validates");
}

}  // namespace

int main() {
  StandardModelFiltersCandidatesDeterministically();
  PrivateModelNeverExposesInputs();
  ShortcutCapacityAndCorruptConfigAreBounded();
  RequestAllowlistIsExact();
  ResourceIsEscapedDeterministicAndNetworkClosed();
  InvalidStringsFailClosed();
  LocalizedStringsComeFromValidatedResources();
  return 0;
}
