#include "crayon/browser_markdown_runtime/highlight_extension.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include "crayon/browser_markdown_runtime/extension_registry.h"
#include "highlight_assets_generated.h"

namespace crayon::browser_markdown_runtime {
namespace {

using internal::EmbeddedHighlightLanguage;

std::vector<std::string> SplitCsv(std::string_view csv) {
  std::vector<std::string> values;
  std::size_t start = 0;
  while (start < csv.size()) {
    const std::size_t separator = csv.find(',', start);
    values.emplace_back(csv.substr(start, separator == std::string_view::npos
                                              ? csv.size() - start
                                              : separator - start));
    if (separator == std::string_view::npos) {
      break;
    }
    start = separator + 1;
  }
  return values;
}

const EmbeddedHighlightLanguage* FindLanguage(std::string_view canonical_id) {
  for (const auto& language : internal::kEmbeddedHighlightLanguages) {
    if (canonical_id == language.canonical_id) {
      return &language;
    }
  }
  return nullptr;
}

bool AppendLoadOrder(std::string_view canonical_id,
                     std::set<std::string>* visiting,
                     std::set<std::string>* visited,
                     std::vector<std::string>* load_order) {
  const std::string id(canonical_id);
  if (visited->find(id) != visited->end()) {
    return true;
  }
  if (visiting->find(id) != visiting->end()) {
    return false;
  }
  const auto* language = FindLanguage(canonical_id);
  if (language == nullptr) {
    return false;
  }
  visiting->insert(id);
  for (const std::string& dependency : SplitCsv(language->dependencies_csv)) {
    if (!AppendLoadOrder(dependency, visiting, visited, load_order)) {
      return false;
    }
  }
  visiting->erase(id);
  visited->insert(id);
  load_order->push_back(id);
  return true;
}

ExtensionManifest HighlightManifest() {
  ExtensionManifest manifest;
  manifest.schema = kManifestSchemaV1;
  manifest.id = kHighlightExtensionId;
  manifest.version = kHighlightExtensionVersion;
  manifest.node_kind = browser_markdown::ExtensionNodeKind::kFence;
  for (const auto& language : internal::kEmbeddedHighlightLanguages) {
    manifest.matchers.emplace_back(language.canonical_id);
  }
  manifest.output = ExtensionOutputKind::kSafeHtml;
  manifest.asset_manifest = kHighlightAssetManifestId;
  manifest.policy_version = ExtensionPolicyVersion::kSafeHtmlV1;
  return manifest;
}

std::shared_ptr<const ExtensionRegistry> HighlightRegistry() {
  static const auto registry = [] {
    const std::vector<ExtensionManifest> manifests = {HighlightManifest()};
    const std::vector<ExtensionAdapterRegistration> adapters = {
        {kHighlightExtensionId, kHighlightExtensionVersion}};
    return BuildExtensionRegistry(/*extension_generation=*/1, manifests,
                                  adapters)
        .registry;
  }();
  return registry;
}

bool DecorateCodeBlocks(const browser_markdown::MarkdownRenderPlan& plan,
                        const std::vector<std::string>& original_matchers,
                        const std::vector<HighlightLanguagePlan>& languages,
                        const RoutePlanResult& routes, std::string* html) {
  if (plan.extension_nodes.size() != languages.size() ||
      plan.extension_nodes.size() != original_matchers.size() ||
      plan.extension_nodes.size() != routes.decisions.size()) {
    return false;
  }
  std::vector<std::size_t> openings;
  openings.reserve(plan.extension_nodes.size());
  std::size_t cursor = 0;
  for (std::size_t index = 0; index < plan.extension_nodes.size(); ++index) {
    const auto& node = plan.extension_nodes[index];
    const auto& language = languages[index];
    const auto& route = routes.decisions[index];
    if (language.kind != HighlightFenceKind::kGrammar ||
        route.status != RouteStatus::kRouted || !route.extension.has_value() ||
        route.extension->extension_id != kHighlightExtensionId ||
        route.node_id != node.node_id) {
      return false;
    }
    const std::string opening =
        "<pre><code class=\"language-" + original_matchers[index] + "\">";
    const std::size_t found = html->find(opening, cursor);
    if (found == std::string::npos) {
      return false;
    }
    openings.push_back(found);
    cursor = found + opening.size();
  }
  for (std::size_t index = plan.extension_nodes.size(); index-- > 0;) {
    const auto& node = plan.extension_nodes[index];
    const auto& language = languages[index];
    const std::string opening =
        "<pre><code class=\"language-" + original_matchers[index] + "\">";
    const std::string decorated =
        "<pre><code class=\"language-" + original_matchers[index] +
        " hljs\" data-mdv-highlight=\"" + language.canonical_id +
        "\" data-mdv-node=\"" + node.node_id + "\">";
    html->replace(openings[index], opening.size(), decorated);
  }
  return true;
}

}  // namespace

HighlightLanguagePlan ResolveHighlightFence(const std::string& matcher) {
  for (const std::string& plaintext :
       SplitCsv(internal::kEmbeddedHighlightPlaintextAliases)) {
    if (matcher == plaintext) {
      return {HighlightFenceKind::kPlaintext, {}, {}};
    }
  }
  for (const auto& language : internal::kEmbeddedHighlightLanguages) {
    const auto aliases = SplitCsv(language.aliases_csv);
    if (std::find(aliases.begin(), aliases.end(), matcher) == aliases.end()) {
      continue;
    }
    HighlightLanguagePlan plan;
    plan.kind = HighlightFenceKind::kGrammar;
    plan.canonical_id = language.canonical_id;
    std::set<std::string> visiting;
    std::set<std::string> visited;
    if (!AppendLoadOrder(plan.canonical_id, &visiting, &visited,
                         &plan.load_order)) {
      return {};
    }
    return plan;
  }
  return {};
}

const std::vector<browser_markdown::ExtensionMatcher>&
HighlightFenceSelection() {
  static const std::vector<browser_markdown::ExtensionMatcher> selection = [] {
    std::vector<browser_markdown::ExtensionMatcher> matchers;
    for (const auto& language : internal::kEmbeddedHighlightLanguages) {
      for (std::string alias : SplitCsv(language.aliases_csv)) {
        matchers.push_back(
            {browser_markdown::ExtensionNodeKind::kFence, std::move(alias)});
      }
    }
    return matchers;
  }();
  return selection;
}

AssetCatalogBuildResult BuildHighlightAssetCatalog() {
  RuntimeAssetBundle bundle;
  bundle.manifest_id = kHighlightAssetManifestId;
  bundle.extension_id = kHighlightExtensionId;
  bundle.extension_version = kHighlightExtensionVersion;
  bundle.entry_resource_id = kHighlightAdapterResourceId;
  bundle.resources.reserve(std::size(internal::kEmbeddedHighlightAssets));
  for (const auto& embedded : internal::kEmbeddedHighlightAssets) {
    bundle.resources.push_back({embedded.resource_id,
                                RuntimeAssetContentType::kJavaScript,
                                std::string(embedded.bytes, embedded.size)});
  }
  std::vector<RuntimeAssetBundle> bundles;
  bundles.push_back(std::move(bundle));
  return BuildRuntimeAssetCatalog(std::move(bundles));
}

HighlightDocumentResult RenderHighlightDocument(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision) {
  HighlightDocumentResult result;
  auto plan = browser_markdown::RenderMarkdownPlan(
      input, document_generation, source_revision, HighlightFenceSelection());
  result.render_status = plan.render_status;
  result.facts_status = plan.facts_status;
  if (plan.render_status != browser_markdown::RenderStatus::kOk ||
      plan.facts_status != browser_markdown::ExtensionFactsStatus::kComplete ||
      plan.extension_nodes.empty()) {
    result.safe_html = std::move(plan.safe_html);
    return result;
  }

  std::vector<HighlightLanguagePlan> languages;
  std::vector<std::string> original_matchers;
  languages.reserve(plan.extension_nodes.size());
  original_matchers.reserve(plan.extension_nodes.size());
  for (auto& node : plan.extension_nodes) {
    original_matchers.push_back(node.matcher);
    HighlightLanguagePlan language = ResolveHighlightFence(node.matcher);
    if (language.kind != HighlightFenceKind::kGrammar) {
      result.safe_html = std::move(plan.safe_html);
      return result;
    }
    node.matcher = language.canonical_id;
    languages.push_back(std::move(language));
  }

  const auto registry = HighlightRegistry();
  if (!registry) {
    result.safe_html = std::move(plan.safe_html);
    return result;
  }
  const RoutePlanResult routes = registry->Route(plan);
  std::string decorated = std::move(plan.safe_html);
  if (!DecorateCodeBlocks(plan, original_matchers, languages, routes,
                          &decorated)) {
    result.safe_html = std::move(decorated);
    return result;
  }
  result.safe_html = std::move(decorated);
  result.decorated_blocks = plan.extension_nodes.size();
  return result;
}

}  // namespace crayon::browser_markdown_runtime
