#include "crayon/browser_markdown_runtime/extension_registry.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace crayon::browser_markdown_runtime {
namespace {

bool IsKnownKind(browser_markdown::ExtensionNodeKind kind) {
  switch (kind) {
    case browser_markdown::ExtensionNodeKind::kInline:
    case browser_markdown::ExtensionNodeKind::kBlock:
    case browser_markdown::ExtensionNodeKind::kFence:
    case browser_markdown::ExtensionNodeKind::kContainer:
      return true;
  }
  return false;
}

bool IsLowerKebabToken(const std::string& value, std::size_t max_bytes) {
  if (value.empty() || value.size() > max_bytes) {
    return false;
  }
  const auto is_lower_or_digit = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
  };
  if (!is_lower_or_digit(static_cast<unsigned char>(value.front())) ||
      !is_lower_or_digit(static_cast<unsigned char>(value.back()))) {
    return false;
  }
  bool previous_hyphen = false;
  for (const char character : value) {
    const unsigned char c = static_cast<unsigned char>(character);
    if (is_lower_or_digit(c)) {
      previous_hyphen = false;
      continue;
    }
    if (c != '-' || previous_hyphen) {
      return false;
    }
    previous_hyphen = true;
  }
  return true;
}

bool IsNumericIdentifier(std::string_view identifier) {
  if (identifier.empty() ||
      (identifier.size() > 1 && identifier.front() == '0')) {
    return false;
  }
  return std::all_of(identifier.begin(), identifier.end(), [](char character) {
    return character >= '0' && character <= '9';
  });
}

bool IsSemverIdentifierList(std::string_view value,
                            bool reject_numeric_leading_zero) {
  if (value.empty()) {
    return false;
  }
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find('.', start);
    const std::string_view identifier =
        value.substr(start, end == std::string_view::npos ? value.size() - start
                                                          : end - start);
    if (identifier.empty() ||
        !std::all_of(identifier.begin(), identifier.end(),
                     [](char character) {
                       const unsigned char c =
                           static_cast<unsigned char>(character);
                       return (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-';
                     }) ||
        (reject_numeric_leading_zero &&
         std::all_of(identifier.begin(), identifier.end(),
                     [](char character) {
                       return character >= '0' && character <= '9';
                     }) &&
         !IsNumericIdentifier(identifier))) {
      return false;
    }
    if (end == std::string_view::npos) {
      return true;
    }
    start = end + 1;
  }
  return false;
}

bool IsLockedVersion(const std::string& version) {
  if (version.empty() || version.size() > kMaxManifestVersionBytes) {
    return false;
  }
  const std::string_view text(version);
  const std::size_t build_separator = text.find('+');
  if (build_separator != std::string_view::npos &&
      (text.find('+', build_separator + 1) != std::string_view::npos ||
       !IsSemverIdentifierList(text.substr(build_separator + 1), false))) {
    return false;
  }
  const std::string_view without_build = text.substr(0, build_separator);
  const std::size_t prerelease_separator = without_build.find('-');
  if (prerelease_separator != std::string_view::npos &&
      !IsSemverIdentifierList(without_build.substr(prerelease_separator + 1),
                              true)) {
    return false;
  }
  const std::string_view core = without_build.substr(0, prerelease_separator);
  const std::size_t first_dot = core.find('.');
  const std::size_t second_dot = first_dot == std::string_view::npos
                                     ? std::string_view::npos
                                     : core.find('.', first_dot + 1);
  return first_dot != std::string_view::npos &&
         second_dot != std::string_view::npos &&
         core.find('.', second_dot + 1) == std::string_view::npos &&
         IsNumericIdentifier(core.substr(0, first_dot)) &&
         IsNumericIdentifier(
             core.substr(first_dot + 1, second_dot - first_dot - 1)) &&
         IsNumericIdentifier(core.substr(second_dot + 1));
}

bool CapabilitiesAreDenied(const ExtensionCapabilities& capabilities) {
  return capabilities.network == CapabilityValue::kDeny &&
         capabilities.file == CapabilityValue::kDeny &&
         capabilities.dynamic_code == CapabilityValue::kDeny &&
         capabilities.external_process == CapabilityValue::kDeny &&
         capabilities.export_data == CapabilityValue::kDeny &&
         capabilities.interaction == CapabilityValue::kDeny;
}

bool OutputMatchesPolicy(ExtensionOutputKind output,
                         ExtensionPolicyVersion policy) {
  switch (output) {
    case ExtensionOutputKind::kUnknown:
      return false;
    case ExtensionOutputKind::kSafeHtml:
      return policy == ExtensionPolicyVersion::kSafeHtmlV1;
    case ExtensionOutputKind::kSvg:
      return policy == ExtensionPolicyVersion::kSvgV1;
    case ExtensionOutputKind::kCanvas:
      return policy == ExtensionPolicyVersion::kCanvasV1;
    case ExtensionOutputKind::kError:
      return policy == ExtensionPolicyVersion::kErrorV1;
  }
  return false;
}

bool ManifestIsStructurallyValid(const ExtensionManifest& manifest) {
  if (manifest.schema != kManifestSchemaV1 ||
      !IsLowerKebabToken(manifest.id, kMaxManifestIdBytes) ||
      !IsLockedVersion(manifest.version) || !manifest.node_kind.has_value() ||
      !IsKnownKind(*manifest.node_kind) || manifest.matchers.empty() ||
      manifest.matchers.size() > kMaxMatchersPerManifest ||
      !CapabilitiesAreDenied(manifest.capabilities)) {
    return false;
  }
  if (!manifest.asset_manifest.empty() &&
      !IsLowerKebabToken(manifest.asset_manifest, kMaxAssetManifestIdBytes)) {
    return false;
  }
  std::set<std::string> unique_matchers;
  for (const std::string& matcher : manifest.matchers) {
    if (!browser_markdown::IsValidExtensionMatcherToken(matcher) ||
        !unique_matchers.insert(matcher).second) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct ExtensionRegistry::Impl {
  struct RouteKey final {
    browser_markdown::ExtensionNodeKind kind =
        browser_markdown::ExtensionNodeKind::kFence;
    std::string matcher;

    bool operator<(const RouteKey& other) const {
      if (kind != other.kind) {
        return static_cast<int>(kind) < static_cast<int>(other.kind);
      }
      return matcher < other.matcher;
    }
  };

  enum class EntryState {
    kEnabled = 0,
    kDisabled,
    kConflict,
  };

  struct Entry final {
    EntryState state = EntryState::kDisabled;
    ExtensionDescriptor descriptor;
  };

  std::uint64_t extension_generation = 0;
  std::map<RouteKey, Entry> entries;
};

ExtensionRegistry::ExtensionRegistry(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

ExtensionRegistry::~ExtensionRegistry() = default;

std::uint64_t ExtensionRegistry::extension_generation() const noexcept {
  return impl_->extension_generation;
}

RoutePlanResult ExtensionRegistry::Route(
    const browser_markdown::MarkdownRenderPlan& plan) const {
  RoutePlanResult result;
  if (plan.extension_nodes.size() > browser_markdown::kMaxExtensionNodes) {
    result.status = RoutePlanStatus::kBudgetExceeded;
    return result;
  }

  std::size_t total_source_bytes = 0;
  for (const browser_markdown::ExtensionNode& node : plan.extension_nodes) {
    if (node.source_utf8.size() >
            browser_markdown::kMaxExtensionSourceBytesPerNode ||
        node.source_utf8.size() >
            browser_markdown::kMaxTotalExtensionSourceBytes -
                total_source_bytes) {
      result.status = RoutePlanStatus::kBudgetExceeded;
      return result;
    }
    total_source_bytes += node.source_utf8.size();
  }

  const bool plan_allows_facts =
      plan.render_status == browser_markdown::RenderStatus::kOk &&
      (plan.facts_status == browser_markdown::ExtensionFactsStatus::kComplete ||
       plan.facts_status ==
           browser_markdown::ExtensionFactsStatus::kBudgetExceeded);

  std::unordered_map<std::string, std::size_t> node_id_counts;
  node_id_counts.reserve(plan.extension_nodes.size());
  for (const browser_markdown::ExtensionNode& node : plan.extension_nodes) {
    if (!node.node_id.empty() && node.node_id.size() <= kMaxNodeIdBytes) {
      ++node_id_counts[node.node_id];
    }
  }

  result.decisions.reserve(plan.extension_nodes.size());
  for (const browser_markdown::ExtensionNode& node : plan.extension_nodes) {
    RouteDecision decision;
    if (node.node_id.size() <= kMaxNodeIdBytes) {
      decision.node_id = node.node_id;
    }
    if (!plan_allows_facts) {
      decision.status = RouteStatus::kInvalidNode;
    } else if (!IsKnownKind(node.kind)) {
      decision.status = RouteStatus::kUnknownKind;
    } else if (node.node_id.empty() || node.node_id.size() > kMaxNodeIdBytes ||
               node_id_counts[node.node_id] != 1 ||
               !browser_markdown::IsValidExtensionMatcherToken(node.matcher) ||
               node.source_bytes != node.source_utf8.size() ||
               !browser_markdown::IsValidUtf8(node.source_utf8)) {
      decision.status = RouteStatus::kInvalidNode;
    } else if (node.source_revision != plan.source_revision) {
      decision.status = RouteStatus::kStale;
    } else {
      const Impl::RouteKey key{node.kind, node.matcher};
      const auto entry = impl_->entries.find(key);
      if (entry == impl_->entries.end() ||
          entry->second.state == Impl::EntryState::kDisabled) {
        decision.status = RouteStatus::kDisabled;
      } else if (entry->second.state == Impl::EntryState::kConflict) {
        decision.status = RouteStatus::kRegistryConflict;
      } else {
        decision.status = RouteStatus::kRouted;
        decision.extension = entry->second.descriptor;
        decision.extension->document_generation = plan.document_generation;
        decision.extension->source_revision = plan.source_revision;
        decision.extension->extension_generation = impl_->extension_generation;
      }
    }
    result.decisions.push_back(std::move(decision));
  }
  return result;
}

RegistryBuildResult BuildExtensionRegistry(
    std::uint64_t extension_generation,
    const std::vector<ExtensionManifest>& manifests,
    const std::vector<ExtensionAdapterRegistration>& adapters) {
  RegistryBuildResult result;
  if (manifests.size() > kMaxRegistryManifests ||
      adapters.size() > kMaxRegistryManifests) {
    return result;
  }

  std::map<std::string, std::string> adapter_versions;
  for (const ExtensionAdapterRegistration& adapter : adapters) {
    if (!IsLowerKebabToken(adapter.extension_id, kMaxManifestIdBytes) ||
        !IsLockedVersion(adapter.version) ||
        !adapter_versions.emplace(adapter.extension_id, adapter.version)
             .second) {
      return result;
    }
  }

  std::set<std::string> manifest_ids;
  for (const ExtensionManifest& manifest : manifests) {
    if (!ManifestIsStructurallyValid(manifest) ||
        !manifest_ids.insert(manifest.id).second) {
      return result;
    }
  }

  auto impl = std::make_unique<ExtensionRegistry::Impl>();
  impl->extension_generation = extension_generation;
  bool has_conflict = false;
  for (const ExtensionManifest& manifest : manifests) {
    const auto adapter = adapter_versions.find(manifest.id);
    const bool enabled =
        OutputMatchesPolicy(manifest.output, manifest.policy_version) &&
        adapter != adapter_versions.end() &&
        adapter->second == manifest.version;
    ExtensionDescriptor descriptor;
    descriptor.extension_id = manifest.id;
    descriptor.version = manifest.version;
    descriptor.output = manifest.output;
    descriptor.asset_manifest = manifest.asset_manifest;
    descriptor.policy_version = manifest.policy_version;

    for (const std::string& matcher : manifest.matchers) {
      ExtensionRegistry::Impl::RouteKey key{*manifest.node_kind, matcher};
      ExtensionRegistry::Impl::Entry entry;
      entry.state = enabled ? ExtensionRegistry::Impl::EntryState::kEnabled
                            : ExtensionRegistry::Impl::EntryState::kDisabled;
      entry.descriptor = descriptor;
      const auto inserted = impl->entries.emplace(std::move(key), entry);
      if (!inserted.second) {
        inserted.first->second.state =
            ExtensionRegistry::Impl::EntryState::kConflict;
        has_conflict = true;
      }
    }
  }

  result.status = has_conflict ? RegistryBuildStatus::kReadyWithConflicts
                               : RegistryBuildStatus::kReady;
  result.registry = std::shared_ptr<const ExtensionRegistry>(
      new ExtensionRegistry(std::move(impl)));
  return result;
}

}  // namespace crayon::browser_markdown_runtime
