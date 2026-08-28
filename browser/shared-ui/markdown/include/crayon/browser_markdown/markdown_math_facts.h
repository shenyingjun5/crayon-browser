// MRT-08: deterministic KaTeX delimiter facts from the existing md4c flow.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "crayon/browser_markdown/markdown_extension_facts.h"

namespace crayon::browser_markdown {

inline constexpr std::size_t kMaxMathSourceBytes = 64 * 1024;
inline constexpr std::size_t kMaxMathTokens = 8192;
inline constexpr std::size_t kMaxMathBraceDepth = 64;

struct MathExtensionFact final {
  ExtensionNode node;
  std::size_t source_begin = 0;
  std::size_t source_end = 0;
  std::string fallback_utf8;
};

struct MathFactsResult final {
  RenderStatus render_status = RenderStatus::kOk;
  ExtensionFactsStatus facts_status = ExtensionFactsStatus::kComplete;
  std::string normalized_markdown;
  std::vector<MathExtensionFact> facts;
};

/// Collects only the math grammar frozen by math-katex-assets-v1. md4c remains
/// the sole Markdown parser: its LATEXMATH callbacks confirm candidates and
/// exclude code/link destinations; the bounded scanner only tightens delimiter
/// and root-block rules that md4c intentionally leaves permissive.
MathFactsResult CollectMathExtensionFacts(const std::string& input,
                                          std::uint64_t document_generation,
                                          std::uint64_t source_revision);

}  // namespace crayon::browser_markdown
