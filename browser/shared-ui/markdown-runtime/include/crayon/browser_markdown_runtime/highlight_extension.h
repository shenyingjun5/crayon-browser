// MRT-06: closed Code Highlight fence adapter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "crayon/browser_markdown/markdown_extension_facts.h"
#include "crayon/browser_markdown_runtime/runtime_assets.h"

namespace crayon::browser_markdown_runtime {

inline constexpr char kHighlightExtensionId[] = "code-highlight";
inline constexpr char kHighlightExtensionVersion[] = "11.12.0";
inline constexpr char kHighlightAssetManifestId[] = "code-highlight-assets-v1";
inline constexpr char kHighlightAdapterResourceId[] = "adapter";
inline constexpr char kHighlightCoreResourceId[] = "core";
inline constexpr std::size_t kMaxHighlightCandidateBytes = 2 * 1024 * 1024;

enum class HighlightFenceKind {
  kUnsupported = 0,
  kPlaintext,
  kGrammar,
};

struct HighlightLanguagePlan final {
  HighlightFenceKind kind = HighlightFenceKind::kUnsupported;
  std::string canonical_id;
  std::vector<std::string> load_order;
};

struct HighlightDocumentResult final {
  browser_markdown::RenderStatus render_status =
      browser_markdown::RenderStatus::kOk;
  browser_markdown::ExtensionFactsStatus facts_status =
      browser_markdown::ExtensionFactsStatus::kComplete;
  std::string safe_html;
  std::size_t decorated_blocks = 0;
};

/// Exact, case-sensitive resolution of the frozen MRT-05 language/alias set.
HighlightLanguagePlan ResolveHighlightFence(const std::string& matcher);

/// Browser-owned exact parser selection. Plaintext aliases are omitted because
/// they intentionally stay as the unchanged Level A code block.
const std::vector<browser_markdown::ExtensionMatcher>&
HighlightFenceSelection();

/// Builds the immutable catalog from build-time embedded, checked-in assets.
AssetCatalogBuildResult BuildHighlightAssetCatalog();

/// Runs the one md4c render/fact path and adds inert markers only to routed
/// allowlisted fences. Any mismatch returns the unchanged Level A HTML.
HighlightDocumentResult RenderHighlightDocument(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision);

}  // namespace crayon::browser_markdown_runtime
