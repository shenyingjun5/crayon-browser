// MRT-03 / MR-001: closed manifest registry and exact route contract tests.
#include "crayon/browser_markdown_runtime/extension_registry.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace markdown = crayon::browser_markdown;
namespace runtime = crayon::browser_markdown_runtime;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

runtime::ExtensionManifest Manifest(std::string id,
                                    markdown::ExtensionNodeKind kind,
                                    std::vector<std::string> matchers) {
  runtime::ExtensionManifest manifest;
  manifest.schema = runtime::kManifestSchemaV1;
  manifest.id = std::move(id);
  manifest.version = "1.2.3";
  manifest.node_kind = kind;
  manifest.matchers = std::move(matchers);
  manifest.output = runtime::ExtensionOutputKind::kSvg;
  manifest.asset_manifest = "runtime-v1";
  manifest.policy_version = runtime::ExtensionPolicyVersion::kSvgV1;
  return manifest;
}

runtime::ExtensionAdapterRegistration Adapter(std::string id,
                                              std::string version = "1.2.3") {
  return {std::move(id), std::move(version)};
}

markdown::ExtensionNode Node(std::string id, markdown::ExtensionNodeKind kind,
                             std::string matcher,
                             std::string source = "source\n") {
  markdown::ExtensionNode node;
  node.kind = kind;
  node.node_id = std::move(id);
  node.matcher = std::move(matcher);
  node.source_utf8 = std::move(source);
  node.source_bytes = node.source_utf8.size();
  node.source_revision = 9;
  return node;
}

markdown::MarkdownRenderPlan Plan(std::vector<markdown::ExtensionNode> nodes) {
  markdown::MarkdownRenderPlan plan;
  plan.document_generation = 7;
  plan.source_revision = 9;
  plan.extension_nodes = std::move(nodes);
  return plan;
}

bool ValidManifestRoutesExactFourKinds() {
  std::vector<runtime::ExtensionManifest> manifests;
  std::vector<runtime::ExtensionAdapterRegistration> adapters;
  const std::vector<std::pair<markdown::ExtensionNodeKind, std::string>> kinds =
      {
          {markdown::ExtensionNodeKind::kInline, "math-inline"},
          {markdown::ExtensionNodeKind::kBlock, "math-block"},
          {markdown::ExtensionNodeKind::kFence, "mermaid"},
          {markdown::ExtensionNodeKind::kContainer, "tip"},
      };
  std::vector<markdown::ExtensionNode> nodes;
  std::size_t index = 0;
  for (const auto& item : kinds) {
    const std::string id = "extension-" + std::to_string(index);
    manifests.push_back(Manifest(id, item.first, {item.second}));
    adapters.push_back(Adapter(id));
    nodes.push_back(
        Node("node-" + std::to_string(index), item.first, item.second));
    ++index;
  }
  const auto built = runtime::BuildExtensionRegistry(11, manifests, adapters);
  CHECK(built.status == runtime::RegistryBuildStatus::kReady);
  CHECK(built.registry != nullptr);
  CHECK(built.registry->extension_generation() == 11);

  const auto empty = built.registry->Route(Plan({}));
  CHECK(empty.status == runtime::RoutePlanStatus::kComplete);
  CHECK(empty.decisions.empty());

  const auto routed = built.registry->Route(Plan(std::move(nodes)));
  CHECK(routed.status == runtime::RoutePlanStatus::kComplete);
  CHECK(routed.decisions.size() == kinds.size());
  for (const auto& decision : routed.decisions) {
    CHECK(decision.status == runtime::RouteStatus::kRouted);
    CHECK(decision.extension.has_value());
    CHECK(decision.extension->document_generation == 7);
    CHECK(decision.extension->source_revision == 9);
    CHECK(decision.extension->extension_generation == 11);
  }

  const auto exact = built.registry->Route(
      Plan({Node("case", markdown::ExtensionNodeKind::kFence, "Mermaid"),
            Node("extra", markdown::ExtensionNodeKind::kFence, "mermaid extra"),
            Node("unknown", markdown::ExtensionNodeKind::kFence, "unknown")}));
  CHECK(exact.decisions.size() == 3);
  CHECK(exact.decisions[0].status == runtime::RouteStatus::kInvalidNode);
  CHECK(exact.decisions[1].status == runtime::RouteStatus::kInvalidNode);
  CHECK(exact.decisions[2].status == runtime::RouteStatus::kDisabled);
  return true;
}

bool StructuralManifestFailuresPublishNothing() {
  auto valid =
      Manifest("mermaid", markdown::ExtensionNodeKind::kFence, {"mermaid"});
  const auto previous =
      runtime::BuildExtensionRegistry(1, {valid}, {Adapter("mermaid")});
  CHECK(previous.registry != nullptr);
  auto prerelease = valid;
  prerelease.version = "1.2.3-beta.1+build.5";
  const auto locked_prerelease = runtime::BuildExtensionRegistry(
      1, {prerelease}, {Adapter("mermaid", prerelease.version)});
  CHECK(locked_prerelease.registry != nullptr);

  std::vector<runtime::ExtensionManifest> invalid;
  auto wildcard = valid;
  wildcard.matchers = {"mermaid*"};
  invalid.push_back(wildcard);
  auto duplicate_matcher = valid;
  duplicate_matcher.matchers = {"mermaid", "mermaid"};
  invalid.push_back(duplicate_matcher);
  auto unlocked = valid;
  unlocked.version = "latest";
  invalid.push_back(unlocked);
  auto tag = valid;
  tag.version = "main";
  invalid.push_back(tag);
  auto capability = valid;
  capability.capabilities.network = runtime::CapabilityValue::kPageLocal;
  invalid.push_back(capability);
  auto interaction = valid;
  interaction.capabilities.interaction = runtime::CapabilityValue::kPageLocal;
  invalid.push_back(interaction);
  auto asset_path = valid;
  asset_path.asset_manifest = "../runtime.js";
  invalid.push_back(asset_path);
  auto asset_url = valid;
  asset_url.asset_manifest = "runtime-v1?remote=true";
  invalid.push_back(asset_url);
  auto wrong_schema = valid;
  wrong_schema.schema = "crayon.markdown-runtime/manifest/v2";
  invalid.push_back(wrong_schema);
  auto missing_kind = valid;
  missing_kind.node_kind.reset();
  invalid.push_back(missing_kind);

  std::size_t invalid_index = 0;
  for (auto manifest : invalid) {
    manifest.id = "invalid-" + std::to_string(invalid_index++);
    const auto rejected = runtime::BuildExtensionRegistry(
        2, {manifest}, {Adapter(manifest.id, manifest.version)});
    CHECK(rejected.status == runtime::RegistryBuildStatus::kInvalidManifestSet);
    CHECK(rejected.registry == nullptr);
    const auto retained = previous.registry->Route(Plan(
        {Node("retained", markdown::ExtensionNodeKind::kFence, "mermaid")}));
    CHECK(retained.decisions[0].status == runtime::RouteStatus::kRouted);
  }

  auto duplicate_id =
      runtime::BuildExtensionRegistry(2, {valid, valid}, {Adapter("mermaid")});
  CHECK(duplicate_id.registry == nullptr);
  auto duplicate_adapter = runtime::BuildExtensionRegistry(
      2, {valid}, {Adapter("mermaid"), Adapter("mermaid")});
  CHECK(duplicate_adapter.registry == nullptr);
  return true;
}

bool IncompatibleEntriesAreDisabled() {
  auto unknown_output =
      Manifest("unknown-output", markdown::ExtensionNodeKind::kFence, {"uo"});
  unknown_output.output = static_cast<runtime::ExtensionOutputKind>(99);
  auto unknown_policy =
      Manifest("unknown-policy", markdown::ExtensionNodeKind::kFence, {"up"});
  unknown_policy.policy_version =
      static_cast<runtime::ExtensionPolicyVersion>(99);
  auto mismatched_policy =
      Manifest("wrong-policy", markdown::ExtensionNodeKind::kFence, {"wp"});
  mismatched_policy.policy_version = runtime::ExtensionPolicyVersion::kCanvasV1;
  auto missing_adapter =
      Manifest("missing", markdown::ExtensionNodeKind::kFence, {"missing"});
  auto version_mismatch =
      Manifest("version", markdown::ExtensionNodeKind::kFence, {"version"});

  const std::vector<runtime::ExtensionManifest> manifests = {
      unknown_output, unknown_policy, mismatched_policy, missing_adapter,
      version_mismatch};
  const auto built = runtime::BuildExtensionRegistry(
      3, manifests,
      {Adapter("unknown-output"), Adapter("unknown-policy"),
       Adapter("wrong-policy"), Adapter("version", "1.2.4")});
  CHECK(built.status == runtime::RegistryBuildStatus::kReady);
  CHECK(built.registry != nullptr);
  const auto result = built.registry->Route(
      Plan({Node("uo", markdown::ExtensionNodeKind::kFence, "uo"),
            Node("up", markdown::ExtensionNodeKind::kFence, "up"),
            Node("wp", markdown::ExtensionNodeKind::kFence, "wp"),
            Node("missing", markdown::ExtensionNodeKind::kFence, "missing"),
            Node("version", markdown::ExtensionNodeKind::kFence, "version")}));
  CHECK(result.decisions.size() == manifests.size());
  for (const auto& decision : result.decisions) {
    CHECK(decision.status == runtime::RouteStatus::kDisabled);
    CHECK(!decision.extension.has_value());
  }
  return true;
}

bool ConflictsDisableEveryOwnerIndependentlyOfOrder() {
  auto first =
      Manifest("first", markdown::ExtensionNodeKind::kFence, {"shared", "one"});
  auto second = Manifest("second", markdown::ExtensionNodeKind::kFence,
                         {"shared", "two"});
  for (const std::vector<runtime::ExtensionManifest>& manifests :
       {std::vector<runtime::ExtensionManifest>{first, second},
        std::vector<runtime::ExtensionManifest>{second, first}}) {
    const auto built = runtime::BuildExtensionRegistry(
        4, manifests, {Adapter("first"), Adapter("second")});
    CHECK(built.status == runtime::RegistryBuildStatus::kReadyWithConflicts);
    const auto result = built.registry->Route(
        Plan({Node("shared", markdown::ExtensionNodeKind::kFence, "shared"),
              Node("one", markdown::ExtensionNodeKind::kFence, "one"),
              Node("two", markdown::ExtensionNodeKind::kFence, "two")}));
    CHECK(result.decisions[0].status ==
          runtime::RouteStatus::kRegistryConflict);
    CHECK(result.decisions[1].status == runtime::RouteStatus::kRouted);
    CHECK(result.decisions[2].status == runtime::RouteStatus::kRouted);
    CHECK(result.decisions[1].extension->extension_id == "first");
    CHECK(result.decisions[2].extension->extension_id == "second");
  }
  return true;
}

bool InvalidPlansFailBeforeLookup() {
  const auto built = runtime::BuildExtensionRegistry(
      5,
      {Manifest("mermaid", markdown::ExtensionNodeKind::kFence, {"mermaid"})},
      {Adapter("mermaid")});
  CHECK(built.registry != nullptr);

  auto byte_mismatch =
      Node("bytes", markdown::ExtensionNodeKind::kFence, "mermaid");
  ++byte_mismatch.source_bytes;
  auto bad_utf8 = Node("utf8", markdown::ExtensionNodeKind::kFence, "mermaid",
                       std::string("\xC0\xAF"));
  auto stale = Node("stale", markdown::ExtensionNodeKind::kFence, "mermaid");
  stale.source_revision = 8;
  auto unknown_kind =
      Node("kind", static_cast<markdown::ExtensionNodeKind>(99), "mermaid");
  const auto result = built.registry->Route(
      Plan({Node("duplicate", markdown::ExtensionNodeKind::kFence, "mermaid"),
            Node("duplicate", markdown::ExtensionNodeKind::kFence, "mermaid"),
            byte_mismatch, bad_utf8, stale, unknown_kind}));
  CHECK(result.decisions.size() == 6);
  CHECK(result.decisions[0].status == runtime::RouteStatus::kInvalidNode);
  CHECK(result.decisions[1].status == runtime::RouteStatus::kInvalidNode);
  CHECK(result.decisions[2].status == runtime::RouteStatus::kInvalidNode);
  CHECK(result.decisions[3].status == runtime::RouteStatus::kInvalidNode);
  CHECK(result.decisions[4].status == runtime::RouteStatus::kStale);
  CHECK(result.decisions[5].status == runtime::RouteStatus::kUnknownKind);

  auto failed_render =
      Plan({Node("render", markdown::ExtensionNodeKind::kFence, "mermaid")});
  failed_render.render_status = markdown::RenderStatus::kInvalidUtf8;
  auto failed_facts =
      Plan({Node("facts", markdown::ExtensionNodeKind::kFence, "mermaid")});
  failed_facts.facts_status = markdown::ExtensionFactsStatus::kParserFailure;
  for (const auto& invalid_plan : {failed_render, failed_facts}) {
    const auto rejected = built.registry->Route(invalid_plan);
    CHECK(rejected.decisions.size() == 1);
    CHECK(rejected.decisions[0].status == runtime::RouteStatus::kInvalidNode);
    CHECK(!rejected.decisions[0].extension.has_value());
  }
  return true;
}

bool RegistryAndPlanBudgetsAreBounded() {
  auto too_many_matchers = Manifest(
      "many", markdown::ExtensionNodeKind::kFence,
      std::vector<std::string>(runtime::kMaxMatchersPerManifest + 1, "many"));
  auto built = runtime::BuildExtensionRegistry(6, {too_many_matchers},
                                               {Adapter("many")});
  CHECK(built.registry == nullptr);

  std::vector<runtime::ExtensionManifest> too_many_manifests;
  std::vector<runtime::ExtensionAdapterRegistration> too_many_adapters;
  for (std::size_t i = 0; i < runtime::kMaxRegistryManifests + 1; ++i) {
    const std::string suffix = std::to_string(i);
    too_many_manifests.push_back(Manifest("extension-" + suffix,
                                          markdown::ExtensionNodeKind::kFence,
                                          {"matcher-" + suffix}));
    too_many_adapters.push_back(Adapter("extension-" + suffix));
  }
  built = runtime::BuildExtensionRegistry(6, too_many_manifests, {});
  CHECK(built.registry == nullptr);
  built = runtime::BuildExtensionRegistry(6, {}, too_many_adapters);
  CHECK(built.registry == nullptr);

  auto long_id = Manifest(std::string(runtime::kMaxManifestIdBytes + 1, 'a'),
                          markdown::ExtensionNodeKind::kFence, {"long-id"});
  built = runtime::BuildExtensionRegistry(6, {long_id}, {});
  CHECK(built.registry == nullptr);

  const auto valid = runtime::BuildExtensionRegistry(
      6,
      {Manifest("mermaid", markdown::ExtensionNodeKind::kFence, {"mermaid"})},
      {Adapter("mermaid")});
  std::vector<markdown::ExtensionNode> nodes;
  nodes.reserve(markdown::kMaxExtensionNodes + 1);
  for (std::size_t i = 0; i < markdown::kMaxExtensionNodes + 1; ++i) {
    nodes.push_back(Node("node-" + std::to_string(i),
                         markdown::ExtensionNodeKind::kFence, "mermaid"));
  }
  const auto result = valid.registry->Route(Plan(std::move(nodes)));
  CHECK(result.status == runtime::RoutePlanStatus::kBudgetExceeded);
  CHECK(result.decisions.empty());
  return true;
}

}  // namespace

int main() {
  const bool ok = ValidManifestRoutesExactFourKinds() &&
                  StructuralManifestFailuresPublishNothing() &&
                  IncompatibleEntriesAreDisabled() &&
                  ConflictsDisableEveryOwnerIndependentlyOfOrder() &&
                  InvalidPlansFailBeforeLookup() &&
                  RegistryAndPlanBudgetsAreBounded();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "extension_registry_test passed\n";
  return EXIT_SUCCESS;
}
