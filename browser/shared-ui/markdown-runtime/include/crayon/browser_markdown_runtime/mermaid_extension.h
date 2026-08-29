// MDV-15: closed Mermaid fence adapter registration and bounded DSL facts.
// The adapter owns the exact `mermaid` fence matcher and inert placeholder
// markers; loading, SVG rendering and theming stay with MDV-16/17.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "crayon/browser_markdown/markdown_extension_facts.h"

namespace crayon::browser_markdown_runtime {

inline constexpr char kMermaidExtensionId[] = "mermaid";
inline constexpr char kMermaidExtensionVersion[] = "11.17.2";
inline constexpr char kMermaidAssetManifestId[] = "mermaid-runtime-assets-v1";

// Named document-level DSL budgets (contract markdown-viewer.md §16; the
// frozen defaults for runtime concurrency/cache stay with MDV-19). Blocks
// beyond any budget degrade to plain escaped code blocks; the document never
// fails because of them.
inline constexpr std::size_t kMaxMermaidBlocksPerDocument = 64;
inline constexpr std::size_t kMaxMermaidBlockBytes = 64 * 1024;
inline constexpr std::size_t kMaxTotalMermaidBytes = 512 * 1024;

/// The single exact, case-sensitive fence matcher for Mermaid diagrams.
/// Uppercase, padded or extended info strings never enter the extension.
const std::vector<browser_markdown::ExtensionMatcher>& MermaidFenceSelection();

struct MermaidDecorationResult final {
  /// False means the HTML was left untouched (fail closed); ordinary
  /// Markdown without Mermaid fences is applied=true with zero blocks.
  bool applied = true;
  std::size_t decorated_blocks = 0;
};

/// Marks every budget-allowed, registry-routed ```mermaid fence in `html`
/// with an inert Browser-owned placeholder marker
/// (`data-mdv-mermaid`/`data-mdv-node`), keeping the DSL as escaped text.
/// The DSL never enters URLs, script literals or logs.
MermaidDecorationResult ApplyMermaidDecorations(
    std::string* html, const std::string& input,
    std::uint64_t document_generation, std::uint64_t source_revision);

}  // namespace crayon::browser_markdown_runtime
