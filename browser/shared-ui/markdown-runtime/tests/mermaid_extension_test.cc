// MDV-15 / MD-002 / MD-009: closed Mermaid fence adapter contracts.
#include "crayon/browser_markdown_runtime/katex_extension.h"
#include "crayon/browser_markdown_runtime/mermaid_extension.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using crayon::browser_markdown_runtime::ApplyMermaidDecorations;
using crayon::browser_markdown_runtime::kMaxMermaidBlocksPerDocument;
using crayon::browser_markdown_runtime::MermaidDecorationResult;
using crayon::browser_markdown_runtime::RenderP0MarkdownDocument;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

std::size_t CountOccurrences(const std::string& html,
                             const std::string& needle) {
  std::size_t count = 0;
  std::size_t cursor = 0;
  while ((cursor = html.find(needle, cursor)) != std::string::npos) {
    ++count;
    cursor += needle.size();
  }
  return count;
}

std::string FirstMarkerId(const std::string& html) {
  const std::string key = "data-mdv-node=\"";
  const std::size_t at = html.find(key);
  if (at == std::string::npos) {
    return std::string();
  }
  const std::size_t begin = at + key.size();
  const std::size_t end = html.find('"', begin);
  return end == std::string::npos ? std::string()
                                  : html.substr(begin, end - begin);
}

/// Seven contract-required diagram DSLs all route to the single `mermaid`
/// kind with per-block opaque node IDs.
bool SevenDiagramKindsShareOneAdapter() {
  const char* dsls[] = {
      "flowchart TD\n  A[Start] --> B[End]\n",
      "sequenceDiagram\n  Alice->>Bob: hello\n",
      "classDiagram\n  Animal <|-- Dog\n",
      "stateDiagram-v2\n  [*] --> idle\n",
      "erDiagram\n  USER ||--o{ ORDER : places\n",
      "gantt\n  title Plan\n  section Work\n  Task1 :a1, 2026-01-01, 30d\n",
      "pie title Share\n  \"x\" : 45\n  \"y\" : 55\n",
  };
  std::string input;
  for (const char* dsl : dsls) {
    input += std::string("```mermaid\n") + dsl + "```\n\n";
  }
  const auto result = RenderP0MarkdownDocument(input, 3, 9);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.mermaid_blocks == 7);
  CHECK(CountOccurrences(result.safe_html, "data-mdv-mermaid=\"true\"") == 7);
  CHECK(CountOccurrences(result.safe_html, "language-mermaid\"") == 7);
  // Every block carries a distinct opaque node ID bound to this revision.
  std::size_t cursor = 0;
  std::string ids;
  for (std::size_t index = 0; index < 7; ++index) {
    const std::size_t at =
        result.safe_html.find("data-mdv-node=\"", cursor);
    CHECK(at != std::string::npos);
    const std::size_t begin = at + 15;
    const std::size_t end = result.safe_html.find('"', begin);
    CHECK(end != std::string::npos);
    const std::string id = result.safe_html.substr(begin, end - begin);
    CHECK(!id.empty());
    CHECK(ids.find(id) == std::string::npos);
    ids += id;
    cursor = end;
  }
  // DSL survives as escaped text; no raw angle brackets leak.
  CHECK(result.safe_html.find("Alice-&gt;&gt;Bob") != std::string::npos);
  return true;
}

/// Uppercase, padded and extended info strings never enter the extension.
bool MatcherIsExactAndCaseSensitive() {
  const std::string input =
      "```MERMAID\nflowchart TD\n  A --> B\n```\n\n"
      "```Mermaid\nflowchart TD\n  A --> B\n```\n\n"
      "```mermaid extra\nflowchart TD\n  A --> B\n```\n\n"
      "```mermaidish\nflowchart TD\n  A --> B\n```\n";
  const auto result = RenderP0MarkdownDocument(input, 3, 10);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.mermaid_blocks == 0);
  CHECK(result.safe_html.find("data-mdv-mermaid") == std::string::npos);
  // The fences remain ordinary escaped code blocks.
  CHECK(CountOccurrences(result.safe_html, "flowchart TD") == 4);
  return true;
}

/// Unclosed fences are fenced code blocks per CommonMark: the adapter marks
/// the block but the DSL stays escaped text and any render failure later
/// degrades to the local error card (MDV-17). Hostile DSL never produces
/// live HTML.
bool HostileAndMalformedInputDegradesSafely() {
  const std::string unclosed =
      "# Doc\n\n```mermaid\nflowchart TD\n  A --> B\n";
  auto result = RenderP0MarkdownDocument(unclosed, 3, 11);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.mermaid_blocks == 1);
  CHECK(result.safe_html.find("data-mdv-mermaid=\"true\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("flowchart TD") != std::string::npos);

  const std::string injection =
      "```mermaid\nflowchart TD\n  A[\"<img src=x onerror=alert(1)>\"]\n"
      "  B[\"<script>alert(2)</script>\"]\n```\n";
  result = RenderP0MarkdownDocument(injection, 3, 12);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.safe_html.find("<img src=x") == std::string::npos);
  CHECK(result.safe_html.find("<script>") == std::string::npos);
  CHECK(result.safe_html.find("&lt;script&gt;") != std::string::npos);
  return true;
}

/// Over-budget blocks degrade to plain code blocks one by one; the document
/// never fails.
bool BudgetOverflowDegradesOnlyTheOffendingBlocks() {
  const std::string huge(65 * 1024, 'x');
  const std::string oversize =
      "```mermaid\n" + huge + "\n```\n\n"
      "```mermaid\nflowchart TD\n  A --> B\n```\n";
  auto result = RenderP0MarkdownDocument(oversize, 3, 13);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  // The oversized block stays plain; the small one is decorated.
  CHECK(result.mermaid_blocks == 1);
  CHECK(CountOccurrences(result.safe_html, "data-mdv-mermaid=\"true\"") == 1);

  // Document-level block-count budget.
  std::string many;
  for (std::size_t index = 0; index < kMaxMermaidBlocksPerDocument + 2;
       ++index) {
    many += "```mermaid\nflowchart TD\n  A --> B" +
            std::to_string(index) + "\n```\n\n";
  }
  result = RenderP0MarkdownDocument(many, 3, 14);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.mermaid_blocks == kMaxMermaidBlocksPerDocument);
  return true;
}

/// Ordinary Markdown is byte-for-byte unaffected by the adapter.
bool OrdinaryMarkdownIsUnchanged() {
  const std::string input =
      "# Title\n\nSome *text* with `code`.\n\n"
      "```cpp\nint main() { return 42; }\n```\n";
  const auto with_adapter = RenderP0MarkdownDocument(input, 3, 16);
  CHECK(with_adapter.render_status ==
        crayon::browser_markdown::RenderStatus::kOk);
  CHECK(with_adapter.mermaid_blocks == 0);
  CHECK(with_adapter.safe_html.find("data-mdv-mermaid") == std::string::npos);
  CHECK(with_adapter.safe_html.find("<h1>Title</h1>") != std::string::npos);
  CHECK(with_adapter.decorated_code_blocks == 1);
  return true;
}

/// Composition: math, highlight and mermaid coexist in one document.
bool P0ComposesMathHighlightAndMermaid() {
  const std::string input =
      "# Mixed\n\n"
      "Inline $E=mc^2$ math.\n\n"
      "```cpp\nint main() { return 0; }\n```\n\n"
      "```mermaid\nflowchart TD\n  A --> B\n```\n";
  const auto result = RenderP0MarkdownDocument(input, 3, 17);
  CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
  CHECK(result.math_placeholders == 1);
  CHECK(result.decorated_code_blocks == 1);
  CHECK(result.mermaid_blocks == 1);
  CHECK(result.safe_html.find("data-mdv-math=") != std::string::npos);
  CHECK(result.safe_html.find("data-mdv-highlight=\"cpp\"") !=
        std::string::npos);
  CHECK(result.safe_html.find("data-mdv-mermaid=\"true\"") !=
        std::string::npos);
  return true;
}

/// Deterministic revision storm: node IDs are revision-bound; stale
/// compositions fail closed or carry only fresh IDs, never old ones.
bool RevisionStormNeverPlacesStaleBlocks() {
  std::string input = "```mermaid\nflowchart TD\n  A --> B\n```\n\nbody\n";
  std::uint64_t seed = 0x5eed;
  auto next = [&seed]() {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return seed >> 33;
  };
  std::string previous_id;
  for (int step = 0; step < 5000; ++step) {
    const std::uint64_t revision = static_cast<std::uint64_t>(step) + 1;
    const auto result = RenderP0MarkdownDocument(input, 3, revision);
    CHECK(result.render_status == crayon::browser_markdown::RenderStatus::kOk);
    CHECK(result.mermaid_blocks == 1);
    const std::string current_id = FirstMarkerId(result.safe_html);
    CHECK(!current_id.empty());
    // Revision-bound IDs must never repeat across revisions (no stale block
    // from a previous revision can land on the new document).
    CHECK(current_id != previous_id);
    previous_id = current_id;

    // Stale composition: a plan from a newer revision applied to the current
    // revision's (already decorated) HTML must fail closed and leave the
    // HTML byte-identical — no stale block may be placed.
    std::string stale_html = result.safe_html;
    const MermaidDecorationResult stale = ApplyMermaidDecorations(
        &stale_html, input, 3, revision + 1);
    CHECK(!stale.applied);
    CHECK(stale.decorated_blocks == 0);
    CHECK(stale_html == result.safe_html);

    switch (next() % 3) {
      case 0: {
        const std::size_t body = input.find("body");
        if (body != std::string::npos) {
          input.replace(body, 4, 1, static_cast<char>('a' + (next() % 26)));
        }
        break;
      }
      case 1:
        input += "para " + std::to_string(step) + "\n\n";
        break;
      default:
        input.insert(0, "# h\n");
        break;
    }
    if (input.size() > 4096) {
      input = "```mermaid\nflowchart TD\n  A --> B\n```\n\nbody\n";
    }
  }
  return true;
}

}  // namespace

int main() {
  if (!SevenDiagramKindsShareOneAdapter() ||
      !MatcherIsExactAndCaseSensitive() ||
      !HostileAndMalformedInputDegradesSafely() ||
      !BudgetOverflowDegradesOnlyTheOffendingBlocks() ||
      !OrdinaryMarkdownIsUnchanged() ||
      !P0ComposesMathHighlightAndMermaid() ||
      !RevisionStormNeverPlacesStaleBlocks()) {
    return EXIT_FAILURE;
  }
  std::cout << "mermaid_extension_test passed\n";
  return EXIT_SUCCESS;
}
