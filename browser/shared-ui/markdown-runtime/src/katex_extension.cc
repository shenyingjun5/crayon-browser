#include "crayon/browser_markdown_runtime/katex_extension.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "crayon/browser_markdown/markdown_math_facts.h"
#include "crayon/browser_markdown_runtime/extension_registry.h"
#include "crayon/browser_markdown_runtime/highlight_extension.h"
#include "katex_assets_generated.h"

namespace crayon::browser_markdown_runtime {
namespace {

constexpr char kMarkerPrefix[] = "CRAYONMATHMARKER";

const std::set<std::string>& DeniedCommands() {
  static const std::set<std::string> commands = {
      "href",           "url",         "includegraphics",
      "htmlclass",      "htmlid",      "htmlstyle",
      "htmldata",       "def",         "gdef",
      "edef",           "xdef",        "let",
      "futurelet",      "newcommand",  "renewcommand",
      "providecommand", "global",      "csname",
      "endcsname",      "expandafter", "noexpand",
  };
  return commands;
}

std::string EscapeHtml(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped.push_back(character);
    }
  }
  return escaped;
}

ExtensionManifest KatexManifest(browser_markdown::ExtensionNodeKind kind) {
  ExtensionManifest manifest;
  manifest.schema = kManifestSchemaV1;
  manifest.id = kind == browser_markdown::ExtensionNodeKind::kInline
                    ? kKatexInlineExtensionId
                    : kKatexBlockExtensionId;
  manifest.version = kKatexExtensionVersion;
  manifest.node_kind = kind;
  manifest.matchers = {kind == browser_markdown::ExtensionNodeKind::kInline
                           ? "math-inline"
                           : "math-block"};
  manifest.output = ExtensionOutputKind::kMathHtml;
  manifest.asset_manifest = kKatexAssetManifestId;
  manifest.policy_version = ExtensionPolicyVersion::kMathHtmlV1;
  return manifest;
}

std::shared_ptr<const ExtensionRegistry> KatexRegistry() {
  static const auto registry = [] {
    const std::vector<ExtensionManifest> manifests = {
        KatexManifest(browser_markdown::ExtensionNodeKind::kInline),
        KatexManifest(browser_markdown::ExtensionNodeKind::kBlock)};
    const std::vector<ExtensionAdapterRegistration> adapters = {
        {kKatexInlineExtensionId, kKatexExtensionVersion},
        {kKatexBlockExtensionId, kKatexExtensionVersion}};
    return BuildExtensionRegistry(/*extension_generation=*/1, manifests,
                                  adapters)
        .registry;
  }();
  return registry;
}

std::string MarkerFor(std::size_t ordinal) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string marker = kMarkerPrefix;
  marker.append(16, '0');
  for (std::size_t index = 0; index < 16; ++index) {
    marker[marker.size() - 1 - index] = kHex[ordinal & 0x0f];
    ordinal >>= 4;
  }
  return marker;
}

std::string PlaceholderFor(const browser_markdown::MathExtensionFact& fact) {
  const bool block =
      fact.node.kind == browser_markdown::ExtensionNodeKind::kBlock;
  std::string output = block ? "<div" : "<span";
  output += " class=\"md-math ";
  output += block ? "md-math-block" : "md-math-inline";
  output += "\" data-mdv-math=\"";
  output += block ? "block" : "inline";
  output += "\" data-mdv-node=\"" + EscapeHtml(fact.node.node_id) +
            "\"><span class=\"md-math-input\" hidden>" +
            EscapeHtml(fact.node.source_utf8) +
            "</span><code class=\"md-math-source\">" +
            EscapeHtml(fact.fallback_utf8) + "</code>";
  output += block ? "</div>" : "</span>";
  return output;
}

std::size_t Utf8CodePointBytes(unsigned char lead) {
  if ((lead & 0x80) == 0) {
    return 1;
  }
  if ((lead & 0xe0) == 0xc0) {
    return 2;
  }
  if ((lead & 0xf0) == 0xe0) {
    return 3;
  }
  return 4;
}

bool WithinMathTokenBudget(const std::string& source) {
  std::size_t tokens = 0;
  for (std::size_t index = 0; index < source.size();) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    if (value < 0x80 && std::isspace(value) != 0) {
      ++index;
      continue;
    }
    if (++tokens > browser_markdown::kMaxMathTokens) {
      return false;
    }
    if (source[index] == '\\') {
      ++index;
      if (index == source.size()) {
        continue;
      }
      const unsigned char escaped = static_cast<unsigned char>(source[index]);
      if (std::isalpha(escaped) != 0 || escaped == '@') {
        while (index < source.size()) {
          const unsigned char command =
              static_cast<unsigned char>(source[index]);
          if (std::isalpha(command) == 0 && command != '@') {
            break;
          }
          ++index;
        }
      } else {
        index += Utf8CodePointBytes(escaped);
      }
      continue;
    }
    index += Utf8CodePointBytes(value);
  }
  return true;
}

P0MarkdownDocumentResult HighlightFallback(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision,
    browser_markdown::ExtensionFactsStatus facts_status =
        browser_markdown::ExtensionFactsStatus::kComplete) {
  const HighlightDocumentResult highlighted =
      RenderHighlightDocument(input, document_generation, source_revision);
  P0MarkdownDocumentResult result;
  result.render_status = highlighted.render_status;
  result.facts_status =
      facts_status == browser_markdown::ExtensionFactsStatus::kComplete
          ? highlighted.facts_status
          : facts_status;
  result.safe_html = highlighted.safe_html;
  result.decorated_code_blocks = highlighted.decorated_blocks;
  return result;
}

}  // namespace

KatexSourceStatus ValidateKatexSource(const std::string& source) {
  if (source.empty() || source.size() > browser_markdown::kMaxMathSourceBytes ||
      !browser_markdown::IsValidUtf8(source)) {
    return KatexSourceStatus::kInvalidSource;
  }
  if (!WithinMathTokenBudget(source)) {
    return KatexSourceStatus::kTokenBudget;
  }
  std::size_t depth = 0;
  for (std::size_t index = 0; index < source.size(); ++index) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    if (value == 0 || (value < 0x20 && value != '\n' && value != '\t') ||
        value == 0x7f) {
      return KatexSourceStatus::kInvalidSource;
    }
    if (source[index] == '\\') {
      if (index + 1 < source.size()) {
        ++index;
      }
      continue;
    }
    if (source[index] == '{' &&
        ++depth > browser_markdown::kMaxMathBraceDepth) {
      return KatexSourceStatus::kDepthBudget;
    }
    if (source[index] == '}' && depth > 0) {
      --depth;
    }
  }

  for (std::size_t index = 0; index < source.size(); ++index) {
    if (source[index] != '\\' || index + 1 >= source.size()) {
      continue;
    }
    std::size_t end = index + 1;
    if (std::isalpha(static_cast<unsigned char>(source[end])) != 0 ||
        source[end] == '@') {
      while (end < source.size() &&
             (std::isalpha(static_cast<unsigned char>(source[end])) != 0 ||
              source[end] == '@')) {
        ++end;
      }
    } else {
      ++end;
    }
    std::string command = source.substr(index + 1, end - index - 1);
    std::transform(command.begin(), command.end(), command.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    if (DeniedCommands().find(command) != DeniedCommands().end() ||
        command.rfind("html", 0) == 0) {
      return KatexSourceStatus::kDeniedCommand;
    }
    index = end - 1;
  }
  return KatexSourceStatus::kAllowed;
}

bool IsKatexRuntimeResourceId(const std::string& resource_id) {
  for (const auto& asset : internal::kEmbeddedKatexAssets) {
    if (resource_id == asset.resource_id) {
      return true;
    }
  }
  return false;
}

AssetCatalogBuildResult BuildKatexAssetCatalog() {
  RuntimeAssetBundle bundle;
  bundle.manifest_id = kKatexAssetManifestId;
  bundle.extension_id = kKatexInlineExtensionId;
  bundle.compatible_extension_ids = {kKatexBlockExtensionId};
  bundle.extension_version = kKatexExtensionVersion;
  bundle.entry_resource_id = kKatexAdapterResourceId;
  bundle.resources.reserve(std::size(internal::kEmbeddedKatexAssets));
  for (const auto& embedded : internal::kEmbeddedKatexAssets) {
    bundle.resources.push_back(
        {embedded.resource_id, embedded.content_type,
         std::string(reinterpret_cast<const char*>(embedded.bytes),
                     embedded.size)});
  }
  std::vector<RuntimeAssetBundle> bundles;
  bundles.push_back(std::move(bundle));
  return BuildRuntimeAssetCatalog(std::move(bundles));
}

P0MarkdownDocumentResult RenderP0MarkdownDocument(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision) {
  browser_markdown::MathFactsResult math =
      browser_markdown::CollectMathExtensionFacts(input, document_generation,
                                                  source_revision);
  if (math.render_status != browser_markdown::RenderStatus::kOk) {
    P0MarkdownDocumentResult result;
    result.render_status = math.render_status;
    result.facts_status = math.facts_status;
    return result;
  }
  if (math.facts.empty() ||
      math.normalized_markdown.find(kMarkerPrefix) != std::string::npos) {
    return HighlightFallback(input, document_generation, source_revision,
                             math.facts_status);
  }

  browser_markdown::MarkdownRenderPlan route_plan;
  route_plan.render_status = browser_markdown::RenderStatus::kOk;
  route_plan.facts_status = math.facts_status;
  route_plan.document_generation = document_generation;
  route_plan.source_revision = source_revision;
  std::vector<std::size_t> fact_indices;
  for (std::size_t index = 0; index < math.facts.size(); ++index) {
    if (ValidateKatexSource(math.facts[index].node.source_utf8) ==
        KatexSourceStatus::kAllowed) {
      route_plan.extension_nodes.push_back(math.facts[index].node);
      fact_indices.push_back(index);
    }
  }
  const auto registry = KatexRegistry();
  if (!registry || route_plan.extension_nodes.empty()) {
    return HighlightFallback(input, document_generation, source_revision,
                             math.facts_status);
  }
  const RoutePlanResult routes = registry->Route(route_plan);
  if (routes.decisions.size() != fact_indices.size()) {
    return HighlightFallback(input, document_generation, source_revision,
                             math.facts_status);
  }

  std::vector<std::pair<std::size_t, std::string>> selected;
  for (std::size_t index = 0; index < routes.decisions.size(); ++index) {
    const auto& route = routes.decisions[index];
    const auto& fact = math.facts[fact_indices[index]];
    const std::string expected_id =
        fact.node.kind == browser_markdown::ExtensionNodeKind::kInline
            ? kKatexInlineExtensionId
            : kKatexBlockExtensionId;
    if (route.status == RouteStatus::kRouted && route.extension.has_value() &&
        route.extension->extension_id == expected_id) {
      selected.emplace_back(fact_indices[index], MarkerFor(index));
    }
  }
  if (selected.empty()) {
    return HighlightFallback(input, document_generation, source_revision,
                             math.facts_status);
  }

  std::string masked = math.normalized_markdown;
  for (auto selected_it = selected.rbegin(); selected_it != selected.rend();
       ++selected_it) {
    const auto& fact = math.facts[selected_it->first];
    masked.replace(fact.source_begin, fact.source_end - fact.source_begin,
                   selected_it->second);
  }
  const HighlightDocumentResult highlighted =
      RenderHighlightDocument(masked, document_generation, source_revision);
  if (highlighted.render_status != browser_markdown::RenderStatus::kOk) {
    return HighlightFallback(input, document_generation, source_revision,
                             math.facts_status);
  }

  std::string html = highlighted.safe_html;
  for (const auto& item : selected) {
    const auto& fact = math.facts[item.first];
    const bool block =
        fact.node.kind == browser_markdown::ExtensionNodeKind::kBlock;
    const std::string needle =
        block ? "<p>" + item.second + "</p>" : item.second;
    const std::size_t found = html.find(needle);
    if (found == std::string::npos ||
        html.find(needle, found + needle.size()) != std::string::npos) {
      return HighlightFallback(input, document_generation, source_revision,
                               math.facts_status);
    }
    html.replace(found, needle.size(), PlaceholderFor(fact));
  }

  P0MarkdownDocumentResult result;
  result.render_status = highlighted.render_status;
  result.facts_status = math.facts_status;
  result.safe_html = std::move(html);
  result.decorated_code_blocks = highlighted.decorated_blocks;
  result.math_placeholders = selected.size();
  return result;
}

}  // namespace crayon::browser_markdown_runtime
