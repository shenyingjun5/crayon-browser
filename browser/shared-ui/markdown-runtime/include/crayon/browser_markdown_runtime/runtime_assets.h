// MRT-04: bounded Browser-owned asset catalog. No file or network loading.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace crayon::browser_markdown_runtime {

inline constexpr std::size_t kMaxAssetBundles = 64;
// MDV-16: raised from 64 for the Mermaid Full 104-file closure (contract
// markdown-runtime.md §8 amended 2026-08-29).
inline constexpr std::size_t kMaxAssetsPerBundle = 256;
inline constexpr std::size_t kMaxAssetResourceIdBytes = 64;
inline constexpr std::size_t kMaxRuntimeAssetBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxRuntimeBundleBytes = 32 * 1024 * 1024;
inline constexpr std::size_t kMaxRuntimeCatalogBytes = 64 * 1024 * 1024;

enum class RuntimeAssetContentType {
  kUnknown = 0,
  kJavaScript,
  kCss,
  kWasm,
  kJson,
  kFont,
};

struct RuntimeAsset final {
  std::string resource_id;
  RuntimeAssetContentType content_type = RuntimeAssetContentType::kUnknown;
  std::string bytes;
};

struct RuntimeAssetBundle final {
  std::string manifest_id;
  std::string extension_id;
  /// Additional exact compile-time identities allowed to consume this same
  /// immutable byte closure. Wildcards are intentionally unsupported.
  std::vector<std::string> compatible_extension_ids;
  std::string extension_version;
  std::string entry_resource_id;
  std::vector<RuntimeAsset> resources;
};

enum class AssetCatalogBuildStatus {
  kReady = 0,
  kInvalidCatalog,
  kBudgetExceeded,
};

struct AssetCatalogBuildResult;

class RuntimeAssetCatalog final {
 public:
  RuntimeAssetCatalog(const RuntimeAssetCatalog&) = delete;
  RuntimeAssetCatalog& operator=(const RuntimeAssetCatalog&) = delete;
  ~RuntimeAssetCatalog();

  std::shared_ptr<const RuntimeAssetBundle> FindCompatible(
      const std::string& manifest_id, const std::string& extension_id,
      const std::string& extension_version) const;
  std::size_t bundle_count() const noexcept;
  std::size_t total_bytes() const noexcept;

 private:
  struct Impl;
  explicit RuntimeAssetCatalog(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;

  friend struct AssetCatalogBuildResult;
  friend AssetCatalogBuildResult BuildRuntimeAssetCatalog(
      std::vector<RuntimeAssetBundle>);
};

struct AssetCatalogBuildResult final {
  AssetCatalogBuildStatus status = AssetCatalogBuildStatus::kInvalidCatalog;
  std::shared_ptr<const RuntimeAssetCatalog> catalog;
};

AssetCatalogBuildResult BuildRuntimeAssetCatalog(
    std::vector<RuntimeAssetBundle> bundles);

/// Closed relative resource grammar for embedded bundles. It permits exact
/// nested font paths but rejects traversal, absolute paths and URL syntax.
bool IsValidRuntimeResourceId(const std::string& value);

}  // namespace crayon::browser_markdown_runtime
