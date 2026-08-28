// MRT-03: immutable compile-time Markdown extension registry and exact router.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_markdown/markdown_extension_facts.h"

namespace crayon::browser_markdown_runtime {

inline constexpr char kManifestSchemaV1[] =
    "crayon.markdown-runtime/manifest/v1";
inline constexpr std::size_t kMaxRegistryManifests = 64;
inline constexpr std::size_t kMaxMatchersPerManifest = 32;
inline constexpr std::size_t kMaxManifestIdBytes = 64;
inline constexpr std::size_t kMaxManifestVersionBytes = 64;
inline constexpr std::size_t kMaxAssetManifestIdBytes = 96;
inline constexpr std::size_t kMaxNodeIdBytes = 96;

enum class ExtensionOutputKind {
  kUnknown = 0,
  kSafeHtml,
  kSvg,
  kCanvas,
  kError,
};

enum class ExtensionPolicyVersion {
  kUnknown = 0,
  kSafeHtmlV1,
  kSvgV1,
  kCanvasV1,
  kErrorV1,
};

enum class CapabilityValue {
  kDeny = 0,
  kPageLocal,
};

struct ExtensionCapabilities final {
  CapabilityValue network = CapabilityValue::kDeny;
  CapabilityValue file = CapabilityValue::kDeny;
  CapabilityValue dynamic_code = CapabilityValue::kDeny;
  CapabilityValue external_process = CapabilityValue::kDeny;
  CapabilityValue export_data = CapabilityValue::kDeny;
  CapabilityValue interaction = CapabilityValue::kDeny;
};

struct ExtensionManifest final {
  std::string schema;
  std::string id;
  std::string version;
  std::optional<browser_markdown::ExtensionNodeKind> node_kind;
  std::vector<std::string> matchers;
  ExtensionOutputKind output = ExtensionOutputKind::kUnknown;
  std::string asset_manifest;
  ExtensionPolicyVersion policy_version = ExtensionPolicyVersion::kUnknown;
  ExtensionCapabilities capabilities;
};

// MRT-03 records only the identity/version of a compiled adapter. Factory
// instantiation and runtime lifecycle remain owned by MRT-04.
struct ExtensionAdapterRegistration final {
  std::string extension_id;
  std::string version;
};

enum class RegistryBuildStatus {
  kReady = 0,
  kReadyWithConflicts,
  kInvalidManifestSet,
};

enum class RoutePlanStatus {
  kComplete = 0,
  kBudgetExceeded,
};

enum class RouteStatus {
  kRouted = 0,
  kInvalidNode,
  kUnknownKind,
  kDisabled,
  kRegistryConflict,
  kStale,
};

struct ExtensionDescriptor final {
  std::string extension_id;
  std::string version;
  ExtensionOutputKind output = ExtensionOutputKind::kUnknown;
  std::string asset_manifest;
  ExtensionPolicyVersion policy_version = ExtensionPolicyVersion::kUnknown;
  std::uint64_t document_generation = 0;
  std::uint64_t source_revision = 0;
  std::uint64_t extension_generation = 0;
};

struct RouteDecision final {
  std::string node_id;
  RouteStatus status = RouteStatus::kDisabled;
  std::optional<ExtensionDescriptor> extension;
};

struct RoutePlanResult final {
  RoutePlanStatus status = RoutePlanStatus::kComplete;
  std::vector<RouteDecision> decisions;
};

struct RegistryBuildResult;

class ExtensionRegistry final {
 public:
  ExtensionRegistry(const ExtensionRegistry&) = delete;
  ExtensionRegistry& operator=(const ExtensionRegistry&) = delete;
  ~ExtensionRegistry();

  std::uint64_t extension_generation() const noexcept;
  RoutePlanResult Route(const browser_markdown::MarkdownRenderPlan& plan) const;

 private:
  struct Impl;
  explicit ExtensionRegistry(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;

  friend struct RegistryBuildResult;
  friend RegistryBuildResult BuildExtensionRegistry(
      std::uint64_t, const std::vector<ExtensionManifest>&,
      const std::vector<ExtensionAdapterRegistration>&);
};

struct RegistryBuildResult final {
  RegistryBuildStatus status = RegistryBuildStatus::kInvalidManifestSet;
  std::shared_ptr<const ExtensionRegistry> registry;
};

/// Atomically validates a complete Browser-owned manifest/adapter set. A
/// structurally invalid set returns no snapshot, so the caller can retain its
/// previous immutable registry or keep extensions fully disabled.
RegistryBuildResult BuildExtensionRegistry(
    std::uint64_t extension_generation,
    const std::vector<ExtensionManifest>& manifests,
    const std::vector<ExtensionAdapterRegistration>& adapters);

/// Shared grammar for Browser-owned manifest and asset catalog identifiers.
bool IsValidManifestId(const std::string& value,
                       std::size_t max_bytes = kMaxManifestIdBytes);

/// Exact SemVer validator used by both extension and asset manifests.
bool IsValidLockedVersion(const std::string& version);

/// Closed v1 output-policy compatibility matrix.
bool IsCompatibleOutputPolicy(ExtensionOutputKind output,
                              ExtensionPolicyVersion policy);

}  // namespace crayon::browser_markdown_runtime
