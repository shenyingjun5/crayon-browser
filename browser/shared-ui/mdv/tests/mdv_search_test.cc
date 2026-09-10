// MRT-11 / MR-006: bounded in-document search — case folding, Unicode
// boundary safety, budgets, wrap navigation and clear semantics.

#include "crayon/browser_mdv/mdv_search.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using crayon::browser_mdv::kMaxSearchMatches;
using crayon::browser_mdv::MdvSearchMatch;
using crayon::browser_mdv::MdvSearchResult;
using crayon::browser_mdv::SearchDocument;
using crayon::browser_mdv::SearchStep;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << "CHECK failed: " #condition " at line "  \
                << __LINE__ << std::endl;                   \
      return EXIT_FAILURE;                                  \
    }                                                       \
  } while (false)

int CheckAsciiCaseFolding() {
  const auto result = SearchDocument("Header header HEADER hEaDeR", "header");
  CHECK(result.matches.size() == 4u);
  CHECK(!result.truncated);
  CHECK((result.matches[0] == MdvSearchMatch{0, 6}));
  CHECK((result.matches[3] == MdvSearchMatch{21, 27}));
  return EXIT_SUCCESS;
}

int CheckCjkAndEmojiExactMatch() {
  const std::string source = "搜索功能：搜索一下，再搜索。";
  const auto result = SearchDocument(source, "搜索");
  CHECK(result.matches.size() == 3u);
  // Emoji requires an exact byte sequence and never splits characters.
  const std::string emojied = "a😀b a😀c";
  const auto emoji = SearchDocument(emojied, "😀");
  CHECK(emoji.matches.size() == 2u);
  return EXIT_SUCCESS;
}

int CheckNoCrossCharacterByteMatch() {
  // '中' = E4 B8 AD. A query of the first two bytes must NOT match inside.
  const std::string source = "a\xe4\xb8\xad" "b";
  const auto partial = SearchDocument(source, std::string("\xe4\xb8", 2));
  CHECK(partial.matches.empty());
  const auto full = SearchDocument(source, std::string("\xe4\xb8\xad", 3));
  CHECK(full.matches.size() == 1u);
  // The ASCII prefix byte pattern ending inside the character is rejected.
  const auto prefix_into = SearchDocument(source, std::string("a\xe4", 2));
  CHECK(prefix_into.matches.empty());
  return EXIT_SUCCESS;
}

int CheckOverlappingMatchesDoNotDoubleCount() {
  const auto result = SearchDocument("aaaa", "aa");
  CHECK(result.matches.size() == 2u);
  CHECK((result.matches[0] == MdvSearchMatch{0, 2}));
  CHECK((result.matches[1] == MdvSearchMatch{2, 4}));
  return EXIT_SUCCESS;
}

int CheckMatchBudgetTruncates() {
  std::string source;
  for (int index = 0; index < 300; ++index) {
    source += "hit ";
  }
  const auto result = SearchDocument(source, "hit");
  CHECK(result.matches.size() == kMaxSearchMatches);
  CHECK(result.truncated);
  return EXIT_SUCCESS;
}

int CheckEmptyQueryAndEmptyDocument() {
  const auto empty_query = SearchDocument("content", "");
  CHECK(empty_query.matches.empty());
  CHECK(!empty_query.truncated);
  const auto empty_source = SearchDocument("", "query");
  CHECK(empty_source.matches.empty());
  // Query longer than the source cannot match.
  const auto long_query = SearchDocument("ab", std::string(200, 'x'));
  CHECK(long_query.matches.empty());
  return EXIT_SUCCESS;
}

int CheckWrapNavigationBothDirections() {
  const auto result = SearchDocument("one two one two one", "one");
  CHECK(result.matches.size() == 3u);
  auto stepped = result;
  stepped.cursor = 0;
  CHECK(SearchStep(stepped, -1) == 2u);  // wrap backwards
  stepped.cursor = 2;
  CHECK(SearchStep(stepped, 1) == 0u);   // wrap forwards (stateless step)
  CHECK(SearchStep(stepped, -1) == 1u);  // backwards from 2
  auto forward = result;
  forward.cursor = 0;
  CHECK(SearchStep(forward, 1) == 1u);
  // Single match and empty results stay put.
  const auto single = SearchDocument("only one here", "one");
  CHECK(SearchStep(single, 1) == 0u);
  CHECK(SearchStep(single, -1) == 0u);
  const auto none = SearchDocument("nothing", "one");
  CHECK(SearchStep(none, 1) == 0u);
  return EXIT_SUCCESS;
}

int CheckLargeDocumentStaysBounded() {
  // 5 MiB-class document: scanning remains linear and the budget holds.
  const std::string chunk(1024, 'z');
  std::string source;
  source.reserve(5u * 1024u * 1024u);
  for (int index = 0; index < 5 * 1024; ++index) {
    source += chunk;
  }
  const auto miss = SearchDocument(source, "needle");
  CHECK(miss.matches.empty());
  const auto hit = SearchDocument(source, "zzzz");
  CHECK(hit.matches.size() == kMaxSearchMatches);
  CHECK(hit.truncated);
  return EXIT_SUCCESS;
}

int CheckQueryBudgetRejectsOversize() {
  const std::string query(129, 'a');
  const auto result = SearchDocument(std::string(1024, 'a'), query);
  CHECK(result.matches.empty());
  const auto at_limit = SearchDocument(std::string(128, 'a') + " b", std::string(128, 'a'));
  CHECK(at_limit.matches.size() == 1u);
  return EXIT_SUCCESS;
}

}  // namespace

int main() {
  return CheckAsciiCaseFolding() + CheckCjkAndEmojiExactMatch() +
         CheckNoCrossCharacterByteMatch() +
         CheckOverlappingMatchesDoNotDoubleCount() +
         CheckMatchBudgetTruncates() + CheckEmptyQueryAndEmptyDocument() +
         CheckWrapNavigationBothDirections() +
         CheckLargeDocumentStaysBounded() +
         CheckQueryBudgetRejectsOversize();
}
