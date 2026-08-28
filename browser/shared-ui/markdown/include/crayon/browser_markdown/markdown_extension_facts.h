// MRT-02: bounded ExtensionNode facts derived from the existing md4c parser.
// Facts are inert parser output. Routing, loading and rendering belong to
// MRT-03/04 and are intentionally absent here.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "crayon/browser_markdown/markdown_render.h"

namespace crayon::browser_markdown {

inline constexpr std::size_t kMaxExtensionMatchers = 256;
inline constexpr std::size_t kMaxExtensionMatcherBytes = 64;
inline constexpr std::size_t kMaxExtensionNodes = 1024;
inline constexpr std::size_t kMaxExtensionSourceBytesPerNode = 256 * 1024;
inline constexpr std::size_t kMaxTotalExtensionSourceBytes = 2 * 1024 * 1024;

enum class ExtensionNodeKind {
  kInline = 0,
  kBlock,
  kFence,
  kContainer,
};

struct ExtensionMatcher final {
  ExtensionNodeKind kind = ExtensionNodeKind::kFence;
  std::string token;
};

struct ExtensionNode final {
  ExtensionNodeKind kind = ExtensionNodeKind::kFence;
  std::string node_id;
  std::string matcher;
  std::string source_utf8;
  std::size_t source_bytes = 0;
  std::uint64_t source_revision = 0;
};

enum class ExtensionFactsStatus {
  kComplete = 0,
  kInvalidSelection,
  kBudgetExceeded,
  kParserFailure,
};

struct MarkdownRenderPlan final {
  RenderStatus render_status = RenderStatus::kOk;
  ExtensionFactsStatus facts_status = ExtensionFactsStatus::kComplete;
  std::uint64_t document_generation = 0;
  std::uint64_t source_revision = 0;
  std::string safe_html;
  std::vector<ExtensionNode> extension_nodes;
};

/// Renders the unchanged Level A safe HTML and, only for a non-empty trusted
/// selection, runs md4c's public callbacks to collect exact fenced-code facts.
/// Invalid/over-budget facts never turn a successful safe HTML render into a
/// document failure.
MarkdownRenderPlan RenderMarkdownPlan(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision,
    const std::vector<ExtensionMatcher>& selection = {});

}  // namespace crayon::browser_markdown
