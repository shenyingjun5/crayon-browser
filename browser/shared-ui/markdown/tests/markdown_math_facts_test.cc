// MRT-08 / MR-005: math delimiter facts remain parser-confirmed and bounded.
#include "crayon/browser_markdown/markdown_math_facts.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using crayon::browser_markdown::CollectMathExtensionFacts;
using crayon::browser_markdown::ExtensionFactsStatus;
using crayon::browser_markdown::ExtensionNodeKind;
using crayon::browser_markdown::kMaxMathBraceDepth;
using crayon::browser_markdown::kMaxMathSourceBytes;
using crayon::browser_markdown::kMaxMathTokens;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool InlineMatrixMatchesContract() {
  const auto result = CollectMathExtensionFacts(
      "Text $E = mc^2$ and $5$.\n"
      "\\$x$ $ x$ $x $ US$5$ $x$unit $x +\ny$ ` $code$ `\n",
      7, 9);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.facts_status == ExtensionFactsStatus::kComplete);
  CHECK(result.facts.size() == 2);
  CHECK(result.facts[0].node.kind == ExtensionNodeKind::kInline);
  CHECK(result.facts[0].node.source_utf8 == "E = mc^2");
  CHECK(result.facts[0].fallback_utf8 == "$E = mc^2$");
  CHECK(result.facts[1].node.source_utf8 == "5");
  CHECK(result.facts[0].node.node_id != result.facts[1].node.node_id);
  return true;
}

bool BlocksAreRootOnlyAndPreserveSource() {
  const auto result = CollectMathExtensionFacts(
      "$$\nE = mc^2\n$$\n\n"
      "   $$ x + y $$\n\n"
      "    $$\n    code\n    $$\n\n"
      "- $$\n  list\n  $$\n\n"
      "> $$\n> quote\n> $$\n",
      3, 4);
  CHECK(result.facts.size() == 2);
  CHECK(result.facts[0].node.kind == ExtensionNodeKind::kBlock);
  CHECK(result.facts[0].node.source_utf8 == "E = mc^2");
  CHECK(result.facts[0].fallback_utf8 == "$$\nE = mc^2\n$$");
  CHECK(result.facts[1].node.source_utf8 == "x + y");
  return true;
}

bool LinksCodeBlankAndAlternateFormsStayText() {
  const auto result = CollectMathExtensionFacts(
      "[x]($destination$)\n\n````\n$code$\n````\n\n"
      "$$\nx\n\ny\n$$\n\n$$\nx\n\n\\(z\\) and \\[w\\]",
      1, 2);
  CHECK(result.facts.empty());
  return true;
}

bool BudgetsFailOnlyTheFormula() {
  auto result = CollectMathExtensionFacts(
      "$" + std::string(kMaxMathSourceBytes + 1, 'x') + "$ and $ok$", 1, 3);
  CHECK(result.facts_status == ExtensionFactsStatus::kBudgetExceeded);
  CHECK(result.facts.size() == 1);
  CHECK(result.facts[0].node.source_utf8 == "ok");
  result = CollectMathExtensionFacts(
      "$" + std::string(kMaxMathBraceDepth + 1, '{') + "x$", 1, 4);
  CHECK(result.facts_status == ExtensionFactsStatus::kBudgetExceeded);
  CHECK(result.facts.empty());
  std::string commands;
  commands.reserve((kMaxMathTokens + 1) * 6);
  for (std::size_t index = 0; index <= kMaxMathTokens; ++index) {
    commands += "\\alpha";
  }
  result = CollectMathExtensionFacts("$" + commands + "$ and $ok$", 1, 5);
  CHECK(result.facts_status == ExtensionFactsStatus::kBudgetExceeded);
  CHECK(result.facts.size() == 1);
  CHECK(result.facts[0].node.source_utf8 == "ok");
  return true;
}

bool BomAndCrLfNormalizeDeterministically() {
  const auto result =
      CollectMathExtensionFacts("\xEF\xBB\xBF$$\r\nx + y\r\n$$\r\n", 5, 6);
  CHECK(result.facts.size() == 1);
  CHECK(result.facts[0].node.source_utf8 == "x + y");
  CHECK(result.normalized_markdown == "$$\nx + y\n$$\n");
  return true;
}

bool FrozenSyntaxVectorsAllMatch() {
  struct Expected final {
    ExtensionNodeKind kind;
    std::string source;
  };
  struct Vector final {
    const char* id;
    std::string markdown;
    std::vector<Expected> expected;
  };
  const std::vector<Vector> vectors = {
      {"inline-basic",
       "Text $E = mc^2$ end.",
       {{ExtensionNodeKind::kInline, "E = mc^2"}}},
      {"inline-number", "Value $5$.", {{ExtensionNodeKind::kInline, "5"}}},
      {"inline-escaped-dollar",
       "Text $x + \\$5$ end.",
       {{ExtensionNodeKind::kInline, "x + \\$5"}}},
      {"inline-escaped-opener", "Text \\$x$ end.", {}},
      {"inline-leading-space", "Text $ x$ end.", {}},
      {"inline-trailing-space", "Text $x $ end.", {}},
      {"inline-word-prefix", "US$5$ is text.", {}},
      {"inline-word-suffix", "$x$unit is text.", {}},
      {"inline-newline", "$x +\ny$", {}},
      {"inline-unclosed", "Text $x end.", {}},
      {"inline-code-excluded", "`$x$`", {}},
      {"inline-link-destination-excluded", "[x]($y$)", {}},
      {"block-multiline",
       "$$\nE = mc^2\n$$",
       {{ExtensionNodeKind::kBlock, "E = mc^2"}}},
      {"block-single-line",
       "$$ E = mc^2 $$",
       {{ExtensionNodeKind::kBlock, "E = mc^2"}}},
      {"block-three-space-indent",
       "   $$\n   x + y\n   $$",
       {{ExtensionNodeKind::kBlock, "   x + y"}}},
      {"block-four-space-code", "    $$\n    x\n    $$", {}},
      {"block-list-excluded", "- $$\n  x\n  $$", {}},
      {"block-quote-excluded", "> $$\n> x\n> $$", {}},
      {"block-crosses-blank-paragraph", "$$\nx\n\ny\n$$", {}},
      {"block-unclosed", "$$\nx", {}},
      {"block-extra-delimiter", "$$ x $$ y $$", {}},
      {"alternate-delimiters-disabled", "\\(x\\) and \\[y\\]", {}},
  };
  std::uint64_t revision = 1;
  for (const Vector& vector : vectors) {
    const auto result =
        CollectMathExtensionFacts(vector.markdown, 19, revision++);
    if (result.facts.size() != vector.expected.size()) {
      std::cerr << "syntax vector failed: " << vector.id << '\n';
      return false;
    }
    for (std::size_t index = 0; index < vector.expected.size(); ++index) {
      if (result.facts[index].node.kind != vector.expected[index].kind ||
          result.facts[index].node.source_utf8 !=
              vector.expected[index].source) {
        std::cerr << "syntax vector failed: " << vector.id << '\n';
        return false;
      }
    }
  }
  return true;
}

}  // namespace

int main() {
  if (!InlineMatrixMatchesContract() || !BlocksAreRootOnlyAndPreserveSource() ||
      !LinksCodeBlankAndAlternateFormsStayText() ||
      !BudgetsFailOnlyTheFormula() || !BomAndCrLfNormalizeDeterministically() ||
      !FrozenSyntaxVectorsAllMatch()) {
    return EXIT_FAILURE;
  }
  std::cout << "markdown_math_facts_test passed\n";
  return EXIT_SUCCESS;
}
