// MRT-08: closed KaTeX inline/block adapter and P0 Markdown composition.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "crayon/browser_markdown/markdown_extension_facts.h"
#include "crayon/browser_markdown_runtime/runtime_assets.h"

namespace crayon::browser_markdown_runtime {

inline constexpr char kKatexInlineExtensionId[] = "katex-inline";
inline constexpr char kKatexBlockExtensionId[] = "katex-block";
inline constexpr char kKatexExtensionVersion[] = "0.18.4";
inline constexpr char kKatexAssetManifestId[] = "math-katex-assets-v1";
inline constexpr char kKatexAdapterResourceId[] = "adapter";
inline constexpr char kKatexRuntimeResourceId[] = "katex";
inline constexpr char kKatexStylesheetResourceId[] = "stylesheet";

enum class KatexSourceStatus {
  kAllowed = 0,
  kInvalidSource,
  kTokenBudget,
  kDepthBudget,
  kDeniedCommand,
};

struct P0MarkdownDocumentResult final {
  browser_markdown::RenderStatus render_status =
      browser_markdown::RenderStatus::kOk;
  browser_markdown::ExtensionFactsStatus facts_status =
      browser_markdown::ExtensionFactsStatus::kComplete;
  std::string safe_html;
  std::size_t decorated_code_blocks = 0;
  std::size_t math_placeholders = 0;
  std::size_t mermaid_blocks = 0;
};

KatexSourceStatus ValidateKatexSource(const std::string& source);
bool IsKatexRuntimeResourceId(const std::string& resource_id);
AssetCatalogBuildResult BuildKatexAssetCatalog();

/// Composes math masking/placeholders with the existing Highlight renderer.
/// Ordinary or rejected math stays in the Level A safe output.
P0MarkdownDocumentResult RenderP0MarkdownDocument(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision);

}  // namespace crayon::browser_markdown_runtime
