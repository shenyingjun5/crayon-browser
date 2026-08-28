#include "crayon/browser_markdown_runtime/runtime_assets.h"

#include <map>
#include <set>
#include <utility>

#include "crayon/browser_markdown_runtime/extension_registry.h"

namespace crayon::browser_markdown_runtime {
namespace {

bool IsKnownContentType(RuntimeAssetContentType type) {
  switch (type) {
    case RuntimeAssetContentType::kUnknown:
      return false;
    case RuntimeAssetContentType::kJavaScript:
    case RuntimeAssetContentType::kCss:
    case RuntimeAssetContentType::kWasm:
    case RuntimeAssetContentType::kJson:
    case RuntimeAssetContentType::kFont:
      return true;
  }
  return false;
}

bool IsEntryContentType(RuntimeAssetContentType type) {
  return type == RuntimeAssetContentType::kJavaScript ||
         type == RuntimeAssetContentType::kWasm;
}

}  // namespace

struct RuntimeAssetCatalog::Impl {
  std::map<std::string, std::shared_ptr<const RuntimeAssetBundle>> bundles;
  std::size_t total_bytes = 0;
};

RuntimeAssetCatalog::RuntimeAssetCatalog(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

RuntimeAssetCatalog::~RuntimeAssetCatalog() = default;

std::shared_ptr<const RuntimeAssetBundle> RuntimeAssetCatalog::FindCompatible(
    const std::string& manifest_id, const std::string& extension_id,
    const std::string& extension_version) const {
  const auto found = impl_->bundles.find(manifest_id);
  if (found == impl_->bundles.end() ||
      found->second->extension_id != extension_id ||
      found->second->extension_version != extension_version) {
    return nullptr;
  }
  return found->second;
}

std::size_t RuntimeAssetCatalog::bundle_count() const noexcept {
  return impl_->bundles.size();
}

std::size_t RuntimeAssetCatalog::total_bytes() const noexcept {
  return impl_->total_bytes;
}

AssetCatalogBuildResult BuildRuntimeAssetCatalog(
    std::vector<RuntimeAssetBundle> bundles) {
  AssetCatalogBuildResult result;
  if (bundles.size() > kMaxAssetBundles) {
    result.status = AssetCatalogBuildStatus::kBudgetExceeded;
    return result;
  }

  auto impl = std::make_unique<RuntimeAssetCatalog::Impl>();
  for (RuntimeAssetBundle& bundle : bundles) {
    if (!IsValidManifestId(bundle.manifest_id, kMaxAssetManifestIdBytes) ||
        !IsValidManifestId(bundle.extension_id) ||
        !IsValidLockedVersion(bundle.extension_version) ||
        !IsValidManifestId(bundle.entry_resource_id,
                           kMaxAssetResourceIdBytes) ||
        bundle.resources.empty() ||
        impl->bundles.find(bundle.manifest_id) != impl->bundles.end()) {
      return result;
    }
    if (bundle.resources.size() > kMaxAssetsPerBundle) {
      result.status = AssetCatalogBuildStatus::kBudgetExceeded;
      return result;
    }

    std::set<std::string> resource_ids;
    std::size_t bundle_bytes = 0;
    const RuntimeAsset* entry = nullptr;
    for (const RuntimeAsset& asset : bundle.resources) {
      if (!IsValidManifestId(asset.resource_id, kMaxAssetResourceIdBytes) ||
          !IsKnownContentType(asset.content_type) || asset.bytes.empty() ||
          !resource_ids.insert(asset.resource_id).second) {
        return result;
      }
      if (asset.bytes.size() > kMaxRuntimeAssetBytes) {
        result.status = AssetCatalogBuildStatus::kBudgetExceeded;
        return result;
      }
      if (asset.bytes.size() > kMaxRuntimeBundleBytes - bundle_bytes) {
        result.status = AssetCatalogBuildStatus::kBudgetExceeded;
        return result;
      }
      bundle_bytes += asset.bytes.size();
      if (asset.resource_id == bundle.entry_resource_id) {
        entry = &asset;
      }
    }
    if (entry == nullptr || !IsEntryContentType(entry->content_type)) {
      return result;
    }
    if (bundle_bytes > kMaxRuntimeCatalogBytes - impl->total_bytes) {
      result.status = AssetCatalogBuildStatus::kBudgetExceeded;
      return result;
    }
    impl->total_bytes += bundle_bytes;
    auto stored = std::make_shared<const RuntimeAssetBundle>(std::move(bundle));
    impl->bundles.emplace(stored->manifest_id, std::move(stored));
  }

  result.status = AssetCatalogBuildStatus::kReady;
  result.catalog = std::shared_ptr<const RuntimeAssetCatalog>(
      new RuntimeAssetCatalog(std::move(impl)));
  return result;
}

}  // namespace crayon::browser_markdown_runtime
