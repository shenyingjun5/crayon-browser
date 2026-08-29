// MDV-15: closed Mermaid fence adapter. Registration mirrors the highlight
// adapter; the DSL stays escaped text inside the marked code block and the
// runtime loading/rendering remains with MDV-16/17.
#include "crayon/browser_markdown_runtime/mermaid_extension.h"

#include <iterator>
#include <memory>
#include <utility>

#include "crayon/browser_markdown_runtime/extension_registry.h"
#include "mermaid_assets_generated.h"

namespace crayon::browser_markdown_runtime {
namespace {

constexpr char kMermaidOpening[] = "<pre><code class=\"language-mermaid\">";

ExtensionManifest MermaidManifest() {
  ExtensionManifest manifest;
  manifest.schema = kManifestSchemaV1;
  manifest.id = kMermaidExtensionId;
  manifest.version = kMermaidExtensionVersion;
  manifest.node_kind = browser_markdown::ExtensionNodeKind::kFence;
  manifest.matchers = {"mermaid"};
  // Mermaid diagrams are rendered to SVG by the page runtime; the SVG output
  // must still pass the Browser-owned SVG policy gate before injection.
  manifest.output = ExtensionOutputKind::kSvg;
  manifest.asset_manifest = kMermaidAssetManifestId;
  manifest.policy_version = ExtensionPolicyVersion::kSvgV1;
  return manifest;
}

std::shared_ptr<const ExtensionRegistry> MermaidRegistry() {
  static const auto registry = [] {
    const std::vector<ExtensionManifest> manifests = {MermaidManifest()};
    const std::vector<ExtensionAdapterRegistration> adapters = {
        {kMermaidExtensionId, kMermaidExtensionVersion}};
    return BuildExtensionRegistry(/*extension_generation=*/1, manifests,
                                  adapters)
        .registry;
  }();
  return registry;
}

/// Budget decisions per plan node, in document order. A node stays decorated
/// only while every named budget still has room; anything else degrades to a
/// plain escaped code block.
bool MermaidBudgetAllows(std::size_t index, std::size_t source_bytes,
                         std::size_t* total_bytes) {
  if (index >= kMaxMermaidBlocksPerDocument) {
    return false;
  }
  if (source_bytes > kMaxMermaidBlockBytes) {
    return false;
  }
  if (*total_bytes + source_bytes > kMaxTotalMermaidBytes) {
    return false;
  }
  *total_bytes += source_bytes;
  return true;
}

}  // namespace

const std::vector<browser_markdown::ExtensionMatcher>& MermaidFenceSelection() {
  static const std::vector<browser_markdown::ExtensionMatcher> selection = [] {
    return std::vector<browser_markdown::ExtensionMatcher>{
        {browser_markdown::ExtensionNodeKind::kFence, "mermaid"}};
  }();
  return selection;
}

AssetCatalogBuildResult BuildMermaidAssetCatalog() {
  RuntimeAssetBundle bundle;
  bundle.manifest_id = kMermaidAssetManifestId;
  bundle.extension_id = kMermaidExtensionId;
  bundle.extension_version = kMermaidExtensionVersion;
  bundle.entry_resource_id = "mermaid.esm.min.mjs";
  bundle.resources.reserve(internal::kEmbeddedMermaidAssetCount);
  for (const auto& embedded : internal::kEmbeddedMermaidAssets) {
    bundle.resources.push_back(
        {embedded.resource_id, RuntimeAssetContentType::kJavaScript,
         std::string(reinterpret_cast<const char*>(embedded.bytes),
                     embedded.size)});
  }
  std::vector<RuntimeAssetBundle> bundles;
  bundles.push_back(std::move(bundle));
  return BuildRuntimeAssetCatalog(std::move(bundles));
}

MermaidDecorationResult ApplyMermaidDecorations(
    std::string* html, const std::string& input,
    std::uint64_t document_generation, std::uint64_t source_revision) {
  MermaidDecorationResult result;
  if (html == nullptr) {
    result.applied = false;
    return result;
  }
  browser_markdown::MarkdownRenderPlan plan =
      browser_markdown::RenderMarkdownPlan(
          input, document_generation, source_revision,
          MermaidFenceSelection());
  if (plan.render_status != browser_markdown::RenderStatus::kOk ||
      plan.facts_status !=
          browser_markdown::ExtensionFactsStatus::kComplete) {
    result.applied = false;
    return result;
  }
  if (plan.extension_nodes.empty()) {
    return result;
  }
  const auto registry = MermaidRegistry();
  if (!registry) {
    result.applied = false;
    return result;
  }
  const RoutePlanResult routes = registry->Route(plan);
  if (routes.decisions.size() != plan.extension_nodes.size()) {
    result.applied = false;
    return result;
  }

  // Locate every opening in document order and decide per node. The node
  // order in the plan matches the document order of the openings.
  std::vector<std::size_t> openings;
  openings.reserve(plan.extension_nodes.size());
  std::size_t cursor = 0;
  for (std::size_t index = 0; index < plan.extension_nodes.size(); ++index) {
    const std::size_t found = html->find(kMermaidOpening, cursor);
    if (found == std::string::npos) {
      result.applied = false;
      return result;
    }
    openings.push_back(found);
    cursor = found + std::size(kMermaidOpening) - 1;
  }

  std::size_t total_bytes = 0;
  std::vector<std::pair<std::size_t, std::string>> replacements;
  for (std::size_t index = 0; index < plan.extension_nodes.size(); ++index) {
    const auto& node = plan.extension_nodes[index];
    const auto& route = routes.decisions[index];
    if (route.status != RouteStatus::kRouted || !route.extension.has_value() ||
        route.extension->extension_id != kMermaidExtensionId ||
        route.node_id != node.node_id) {
      continue;
    }
    if (!MermaidBudgetAllows(index, node.source_bytes, &total_bytes)) {
      continue;
    }
    std::string decorated =
        "<pre><code class=\"language-mermaid\" data-mdv-mermaid=\"true\"";
    decorated += " data-mdv-node=\"" + node.node_id + "\">";
    replacements.emplace_back(openings[index], std::move(decorated));
  }

  for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
    html->replace(it->first, std::size(kMermaidOpening) - 1, it->second);
  }
  result.decorated_blocks = replacements.size();
  return result;
}

}  // namespace crayon::browser_markdown_runtime
