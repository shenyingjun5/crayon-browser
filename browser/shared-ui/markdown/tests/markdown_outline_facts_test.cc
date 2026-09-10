// MRT-10 outline facts: heading extraction, bounds, anchor stability and
// determinism over the md4c parse walk.

#include "crayon/browser_markdown/markdown_outline_facts.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using crayon::browser_markdown::CollectOutlineFacts;
using crayon::browser_markdown::kMaxOutlineHeadingTextBytes;
using crayon::browser_markdown::kMaxOutlineHeadings;
using crayon::browser_markdown::OutlineFactsResult;
using crayon::browser_markdown::OutlineFactsStatus;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << "CHECK failed: " #condition " at line "  \
                << __LINE__ << std::endl;                   \
      return EXIT_FAILURE;                                  \
    }                                                       \
  } while (false)

int CheckCollectsLevelsAndPlainText() {
  const std::string input =
      "# Alpha\n\nintro\n\n## Beta **bold**\n\n### Gamma\n\n";
  const OutlineFactsResult result = CollectOutlineFacts(input, 1, 2);
  CHECK(result.status == OutlineFactsStatus::kComplete);
  CHECK(result.headings.size() == 3u);
  CHECK(result.headings[0].level == 1);
  CHECK(result.headings[0].text == "Alpha");
  CHECK(result.headings[1].level == 2);
  CHECK(result.headings[1].text == "Beta bold");
  CHECK(result.headings[2].level == 3);
  CHECK(result.headings[2].text == "Gamma");
  CHECK(result.headings[0].ordinal == 0u);
  CHECK(result.headings[1].ordinal == 1u);
  return EXIT_SUCCESS;
}

int CheckAnchorFormatStableAndRevisionBound() {
  const std::string input = "# Same text\n\n";
  const OutlineFactsResult first = CollectOutlineFacts(input, 0x12, 0x34);
  const OutlineFactsResult again = CollectOutlineFacts(input, 0x12, 0x34);
  const OutlineFactsResult revised = CollectOutlineFacts(input, 0x12, 0x35);
  CHECK(first.headings.size() == 1u);
  CHECK(first.headings[0].anchor == "h-0000000000000012-0000000000000034-0000");
  CHECK(first.headings == again.headings);
  CHECK(first.headings[0].anchor != revised.headings[0].anchor);
  return EXIT_SUCCESS;
}

int CheckOrdinalsAdvanceAcrossHeadings() {
  const std::string input = "# One\n\n## Two\n\n";
  const OutlineFactsResult result = CollectOutlineFacts(input, 7, 9);
  CHECK(result.headings.size() == 2u);
  CHECK(result.headings[0].anchor == "h-0000000000000007-0000000000000009-0000");
  CHECK(result.headings[1].anchor == "h-0000000000000007-0000000000000009-0001");
  return EXIT_SUCCESS;
}

int CheckLongHeadingTextTruncatedWithMarker() {
  const std::string long_text(600, 'x');
  const std::string input = "# " + long_text + "\n\n";
  const OutlineFactsResult result = CollectOutlineFacts(input, 1, 1);
  CHECK(result.headings.size() == 1u);
  CHECK(result.headings[0].text.size() == kMaxOutlineHeadingTextBytes);
  CHECK(result.headings[0].text.substr(
            result.headings[0].text.size() - 3) == "...");
  return EXIT_SUCCESS;
}

int CheckHeadingCountTruncatesGracefully() {
  std::string input;
  for (int index = 0; index < 600; ++index) {
    input += "# H" + std::to_string(index) + "\n\n";
  }
  const OutlineFactsResult result = CollectOutlineFacts(input, 1, 1);
  CHECK(result.headings.size() == kMaxOutlineHeadings);
  CHECK(result.status == OutlineFactsStatus::kComplete);
  CHECK(result.headings.back().text == "H511");
  return EXIT_SUCCESS;
}

int CheckSetextAndDeepLevelsCollected() {
  const std::string input =
      "###### Level six\n\nSetext Title\n"
      "===============\n\nSetext Sub\n---\n\n";
  const OutlineFactsResult result = CollectOutlineFacts(input, 0, 1);
  CHECK(result.headings.size() == 3u);
  CHECK(result.headings[0].level == 6);
  CHECK(result.headings[1].level == 1);
  CHECK(result.headings[2].level == 2);
  CHECK(result.headings[1].text == "Setext Title");
  return EXIT_SUCCESS;
}

int CheckFencedHeadingsNotCollected() {
  const std::string input = "```text\n# not a heading\n```\n\n# Real\n\n";
  const OutlineFactsResult result = CollectOutlineFacts(input, 0, 1);
  CHECK(result.headings.size() == 1u);
  CHECK(result.headings[0].text == "Real");
  return EXIT_SUCCESS;
}

int CheckEmptyDocumentYieldsEmptyOutline() {
  const OutlineFactsResult result = CollectOutlineFacts("", 0, 0);
  CHECK(result.status == OutlineFactsStatus::kComplete);
  CHECK(result.headings.empty());
  return EXIT_SUCCESS;
}

}  // namespace

int main() {
  return CheckCollectsLevelsAndPlainText() +
         CheckAnchorFormatStableAndRevisionBound() +
         CheckOrdinalsAdvanceAcrossHeadings() +
         CheckLongHeadingTextTruncatedWithMarker() +
         CheckHeadingCountTruncatesGracefully() +
         CheckSetextAndDeepLevelsCollected() +
         CheckFencedHeadingsNotCollected() +
         CheckEmptyDocumentYieldsEmptyOutline();
}
