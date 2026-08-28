// MRT-02 / MR-002: ExtensionNode DTO and md4c fence-fact contract tests.
#include "crayon/browser_markdown/markdown_extension_facts.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using crayon::browser_markdown::ExtensionFactsStatus;
using crayon::browser_markdown::ExtensionMatcher;
using crayon::browser_markdown::ExtensionNodeKind;
using crayon::browser_markdown::kMaxExtensionMatcherBytes;
using crayon::browser_markdown::kMaxExtensionMatchers;
using crayon::browser_markdown::kMaxExtensionNodes;
using crayon::browser_markdown::kMaxExtensionSourceBytesPerNode;
using crayon::browser_markdown::kMaxTotalExtensionSourceBytes;
using crayon::browser_markdown::RenderMarkdownPlan;
using crayon::browser_markdown::RenderMarkdownToSafeHtml;
using crayon::browser_markdown::RenderStatus;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

const std::vector<ExtensionMatcher> kMermaidSelection = {
    {ExtensionNodeKind::kFence, "mermaid"}};

bool DefaultIsDisabledAndHtmlIsIdentical() {
  const std::string input = "# Title\n\n```mermaid\nflowchart LR\nA-->B\n```\n";
  const auto plan = RenderMarkdownPlan(input, 7, 19);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kComplete);
  CHECK(plan.safe_html == RenderMarkdownToSafeHtml(input, nullptr));
  CHECK(plan.extension_nodes.empty());
  CHECK(plan.document_generation == 7);
  CHECK(plan.source_revision == 19);
  return true;
}

bool ExactFenceMatching() {
  const std::string input =
      "```mermaid\nflowchart LR\nA-->B\n```\n\n"
      "```Mermaid\ncase-sensitive\n```\n\n"
      "```mermaid extra\nno-extra-token\n```\n\n"
      "```unknown\nfallback\n```\n";
  const auto plan = RenderMarkdownPlan(input, 11, 23, kMermaidSelection);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kComplete);
  CHECK(plan.safe_html == RenderMarkdownToSafeHtml(input, nullptr));
  CHECK(plan.extension_nodes.size() == 1);
  const auto& node = plan.extension_nodes.front();
  CHECK(node.kind == ExtensionNodeKind::kFence);
  CHECK(node.matcher == "mermaid");
  CHECK(node.source_utf8 == "flowchart LR\nA-->B\n");
  CHECK(node.source_bytes == node.source_utf8.size());
  CHECK(node.source_revision == 23);
  CHECK(node.node_id.find("n-") == 0);
  return true;
}

bool NestedAndNormalizedFenceFacts() {
  const std::string input =
      "\xEF\xBB\xBF> ```mermaid\r\n> sequenceDiagram\r\n> A->>B: 中文\r\n> "
      "```\r\n";
  const auto plan = RenderMarkdownPlan(input, 2, 3, kMermaidSelection);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kComplete);
  CHECK(plan.extension_nodes.size() == 1);
  CHECK(plan.extension_nodes[0].source_utf8 ==
        "sequenceDiagram\nA->>B: 中文\n");
  CHECK(plan.extension_nodes[0].source_bytes ==
        plan.extension_nodes[0].source_utf8.size());
  return true;
}

bool FourKindsAreClosedAndUnreviewedKindsEmitNothing() {
  static_assert(ExtensionNodeKind::kInline != ExtensionNodeKind::kBlock);
  static_assert(ExtensionNodeKind::kBlock != ExtensionNodeKind::kFence);
  static_assert(ExtensionNodeKind::kFence != ExtensionNodeKind::kContainer);
  const std::vector<ExtensionMatcher> selection = {
      {ExtensionNodeKind::kInline, "math-inline"},
      {ExtensionNodeKind::kBlock, "math-block"},
      {ExtensionNodeKind::kContainer, "tip"},
  };
  const auto plan = RenderMarkdownPlan(
      "$x$\n\n$$\ny\n$$\n\n:::tip\ntext\n:::\n", 1, 1, selection);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kComplete);
  CHECK(plan.extension_nodes.empty());
  return true;
}

bool InvalidSelectionFailsClosed() {
  const std::string input = "```mermaid\nA-->B\n```\n";
  for (const std::vector<ExtensionMatcher>& selection :
       {std::vector<ExtensionMatcher>{{ExtensionNodeKind::kFence, "Mermaid"}},
        std::vector<ExtensionMatcher>{
            {ExtensionNodeKind::kFence, "mermaid extra"}},
        std::vector<ExtensionMatcher>{
            {static_cast<ExtensionNodeKind>(99), "mermaid"}},
        std::vector<ExtensionMatcher>{
            {ExtensionNodeKind::kFence, "mermaid"},
            {ExtensionNodeKind::kFence, "mermaid"}}}) {
    const auto plan = RenderMarkdownPlan(input, 1, 1, selection);
    CHECK(plan.render_status == RenderStatus::kOk);
    CHECK(plan.facts_status == ExtensionFactsStatus::kInvalidSelection);
    CHECK(plan.extension_nodes.empty());
    CHECK(plan.safe_html == RenderMarkdownToSafeHtml(input, nullptr));
  }

  std::vector<ExtensionMatcher> too_many;
  too_many.reserve(kMaxExtensionMatchers + 1);
  for (std::size_t i = 0; i < kMaxExtensionMatchers + 1; ++i) {
    too_many.push_back({ExtensionNodeKind::kFence, "f" + std::to_string(i)});
  }
  for (const std::vector<ExtensionMatcher>& selection :
       {too_many, std::vector<ExtensionMatcher>{{
                      ExtensionNodeKind::kFence,
                      std::string(kMaxExtensionMatcherBytes + 1, 'm'),
                  }}}) {
    const auto plan = RenderMarkdownPlan(input, 1, 1, selection);
    CHECK(plan.render_status == RenderStatus::kOk);
    CHECK(plan.facts_status == ExtensionFactsStatus::kInvalidSelection);
    CHECK(plan.extension_nodes.empty());
  }
  return true;
}

bool CommonLanguagePunctuationIsBounded() {
  CHECK(crayon::browser_markdown::IsValidExtensionMatcherToken("c++"));
  CHECK(crayon::browser_markdown::IsValidExtensionMatcherToken("c#"));
  CHECK(!crayon::browser_markdown::IsValidExtensionMatcherToken("c/sharp"));
  CHECK(!crayon::browser_markdown::IsValidExtensionMatcherToken("c sharp"));
  return true;
}

bool InputFailuresDoNotProduceFacts() {
  RenderStatus expected = RenderStatus::kInvalidUtf8;
  auto plan =
      RenderMarkdownPlan(std::string("\xC0\xAF"), 1, 1, kMermaidSelection);
  CHECK(plan.render_status == expected);
  CHECK(plan.extension_nodes.empty());
  CHECK(plan.safe_html.empty());
  return true;
}

bool NodeAndSourceBudgetsAreBounded() {
  std::string many;
  for (std::size_t i = 0; i < kMaxExtensionNodes + 1; ++i) {
    many += "```mermaid\nA\n```\n";
  }
  auto plan = RenderMarkdownPlan(many, 3, 5, kMermaidSelection);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kBudgetExceeded);
  CHECK(plan.extension_nodes.size() == kMaxExtensionNodes);

  const std::string oversized =
      "```mermaid\n" + std::string(kMaxExtensionSourceBytesPerNode + 1, 'x') +
      "\n```\n";
  plan = RenderMarkdownPlan(oversized, 3, 6, kMermaidSelection);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kBudgetExceeded);
  CHECK(plan.extension_nodes.empty());

  const std::size_t chunk = kMaxExtensionSourceBytesPerNode / 2;
  std::string total;
  const std::size_t count = kMaxTotalExtensionSourceBytes / chunk + 2;
  for (std::size_t i = 0; i < count; ++i) {
    total += "```mermaid\n" + std::string(chunk, 'y') + "\n```\n";
  }
  plan = RenderMarkdownPlan(total, 3, 7, kMermaidSelection);
  CHECK(plan.render_status == RenderStatus::kOk);
  CHECK(plan.facts_status == ExtensionFactsStatus::kBudgetExceeded);
  std::size_t total_bytes = 0;
  for (const auto& node : plan.extension_nodes) {
    total_bytes += node.source_bytes;
  }
  CHECK(total_bytes <= kMaxTotalExtensionSourceBytes);
  return true;
}

bool NodeIdsAreDeterministicAndGenerationBound() {
  const std::string input = "```mermaid\nA-->B\n```\n```mermaid\nB-->C\n```\n";
  const auto first = RenderMarkdownPlan(input, 8, 13, kMermaidSelection);
  const auto again = RenderMarkdownPlan(input, 8, 13, kMermaidSelection);
  const auto newer = RenderMarkdownPlan(input, 9, 14, kMermaidSelection);
  CHECK(first.extension_nodes.size() == 2);
  CHECK(again.extension_nodes.size() == 2);
  CHECK(newer.extension_nodes.size() == 2);
  CHECK(first.extension_nodes[0].node_id == again.extension_nodes[0].node_id);
  CHECK(first.extension_nodes[0].node_id.size() == 52);
  CHECK(first.extension_nodes[1].node_id == again.extension_nodes[1].node_id);
  CHECK(first.extension_nodes[0].node_id != first.extension_nodes[1].node_id);
  CHECK(first.extension_nodes[0].node_id != newer.extension_nodes[0].node_id);
  return true;
}

}  // namespace

int main() {
  const bool ok = DefaultIsDisabledAndHtmlIsIdentical() &&
                  ExactFenceMatching() && NestedAndNormalizedFenceFacts() &&
                  FourKindsAreClosedAndUnreviewedKindsEmitNothing() &&
                  InvalidSelectionFailsClosed() &&
                  CommonLanguagePunctuationIsBounded() &&
                  InputFailuresDoNotProduceFacts() &&
                  NodeAndSourceBudgetsAreBounded() &&
                  NodeIdsAreDeterministicAndGenerationBound();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "markdown_extension_facts_test passed\n";
  return EXIT_SUCCESS;
}
