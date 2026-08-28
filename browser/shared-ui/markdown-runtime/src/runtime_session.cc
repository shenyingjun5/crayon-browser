#include "crayon/browser_markdown_runtime/runtime_session.h"

#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

namespace crayon::browser_markdown_runtime {
namespace {

bool GenerationsEqual(const RuntimeGenerations& left,
                      const RuntimeGenerations& right) {
  return left.document == right.document && left.source == right.source &&
         left.extension == right.extension;
}

bool IsTerminal(RuntimeRequestState state) {
  return state == RuntimeRequestState::kReady ||
         state == RuntimeRequestState::kFailed ||
         state == RuntimeRequestState::kCancelled ||
         state == RuntimeRequestState::kStale;
}

bool IsActive(RuntimeRequestState state) {
  return state == RuntimeRequestState::kQueued ||
         state == RuntimeRequestState::kLoading ||
         state == RuntimeRequestState::kRendering;
}

bool IsKnownTheme(RuntimeTheme theme) {
  return theme == RuntimeTheme::kLight || theme == RuntimeTheme::kDark;
}

bool IsAllowedFailure(RuntimeError error) {
  return error == RuntimeError::kBudgetExceeded ||
         error == RuntimeError::kAssetUnavailable ||
         error == RuntimeError::kLoadFailed ||
         error == RuntimeError::kRenderFailed ||
         error == RuntimeError::kTimeout ||
         error == RuntimeError::kOutputRejected;
}

RuntimeGenerations DescriptorGenerations(
    const ExtensionDescriptor& descriptor) {
  return {descriptor.document_generation, descriptor.source_revision,
          descriptor.extension_generation};
}

}  // namespace

struct RuntimeSession::Impl {
  struct CacheKeyLess final {
    bool operator()(const RuntimeCacheKey& left,
                    const RuntimeCacheKey& right) const {
      return std::tie(left.profile_isolation, left.document_isolation,
                      left.extension_id, left.extension_version,
                      left.source_digest_present, left.source_digest,
                      left.theme, left.options_digest_present,
                      left.options_digest, left.policy_version) <
             std::tie(right.profile_isolation, right.document_isolation,
                      right.extension_id, right.extension_version,
                      right.source_digest_present, right.source_digest,
                      right.theme, right.options_digest_present,
                      right.options_digest, right.policy_version);
    }
  };

  struct Record final {
    RuntimeRequest request;
    RuntimeRequestState state = RuntimeRequestState::kQueued;
    RuntimeError error = RuntimeError::kNone;
    bool cache_hit = false;
    std::uint64_t result_token = 0;
    std::uint64_t sequence = 0;
    std::shared_ptr<const RuntimeAssetBundle> bundle;
  };

  struct CacheEntry final {
    std::uint64_t result_token = 0;
    std::size_t retained_bytes = 0;
    std::uint64_t last_access_sequence = 0;
  };

  RuntimeGenerations generations;
  std::shared_ptr<const RuntimeAssetCatalog> catalog;
  std::map<std::uint64_t, Record> requests;
  std::map<RuntimeCacheKey, CacheEntry, CacheKeyLess> cache;
  std::size_t active_requests = 0;
  std::size_t pending_requests = 0;
  std::size_t concurrent_requests = 0;
  std::size_t cache_bytes = 0;
  std::uint64_t dropped_requests = 0;
  std::uint64_t next_sequence = 1;
  bool shutdown = false;

  RuntimeRequestSnapshot SnapshotOf(const Record& record) const {
    return {record.request.request_id,
            record.state,
            record.error,
            DescriptorGenerations(record.request.extension),
            record.cache_hit,
            record.result_token};
  }

  RuntimeRequestSnapshot Ephemeral(const RuntimeRequest& request,
                                   RuntimeRequestState state,
                                   RuntimeError error) const {
    return {request.request_id,
            state,
            error,
            DescriptorGenerations(request.extension),
            false,
            0};
  }

  void Finish(Record* record, RuntimeRequestState state, RuntimeError error) {
    if (IsActive(record->state) && active_requests > 0) {
      --active_requests;
    }
    if (record->state == RuntimeRequestState::kQueued && pending_requests > 0) {
      --pending_requests;
    }
    if ((record->state == RuntimeRequestState::kLoading ||
         record->state == RuntimeRequestState::kRendering) &&
        concurrent_requests > 0) {
      --concurrent_requests;
    }
    record->state = state;
    record->error = error;
    record->bundle.reset();
  }

  bool ApplyDeadline(Record* record, std::uint64_t now_ms) {
    if (IsActive(record->state) && now_ms >= record->request.deadline_ms) {
      Finish(record, RuntimeRequestState::kFailed, RuntimeError::kTimeout);
      return true;
    }
    return false;
  }

  void ClearCache() {
    cache.clear();
    cache_bytes = 0;
  }

  void PruneHistory() {
    while (requests.size() >= kMaxRuntimeRequestHistory) {
      auto oldest = requests.end();
      for (auto current = requests.begin(); current != requests.end();
           ++current) {
        if (IsTerminal(current->second.state) &&
            (oldest == requests.end() ||
             current->second.sequence < oldest->second.sequence)) {
          oldest = current;
        }
      }
      if (oldest == requests.end()) {
        return;
      }
      requests.erase(oldest);
    }
  }

  void InsertCache(const RuntimeCacheKey& key, std::uint64_t result_token,
                   std::size_t retained_bytes) {
    if (retained_bytes > kMaxCachedResultBytes) {
      return;
    }
    const auto existing = cache.find(key);
    if (existing != cache.end()) {
      cache_bytes -= existing->second.retained_bytes;
      cache.erase(existing);
    }
    while (!cache.empty() &&
           (cache.size() >= kMaxRuntimeCacheEntries ||
            retained_bytes > kMaxRuntimeCacheBytes - cache_bytes)) {
      const auto oldest = std::min_element(
          cache.begin(), cache.end(), [](const auto& left, const auto& right) {
            return left.second.last_access_sequence <
                   right.second.last_access_sequence;
          });
      cache_bytes -= oldest->second.retained_bytes;
      cache.erase(oldest);
    }
    if (retained_bytes > kMaxRuntimeCacheBytes - cache_bytes) {
      return;
    }
    cache.emplace(key,
                  CacheEntry{result_token, retained_bytes, next_sequence++});
    cache_bytes += retained_bytes;
  }
};

RuntimeSession::RuntimeSession(
    RuntimeGenerations generations,
    std::shared_ptr<const RuntimeAssetCatalog> catalog)
    : impl_(std::make_unique<Impl>()) {
  impl_->generations = generations;
  impl_->catalog = std::move(catalog);
}

RuntimeSession::~RuntimeSession() { Shutdown(); }

RuntimeRequestSnapshot RuntimeSession::Queue(RuntimeRequest request,
                                             std::uint64_t now_ms) {
  if (impl_->shutdown) {
    return impl_->Ephemeral(request, RuntimeRequestState::kCancelled,
                            RuntimeError::kCancelled);
  }
  const RuntimeGenerations request_generations =
      DescriptorGenerations(request.extension);
  const bool structurally_valid =
      request.request_id != 0 && !request.node_id.empty() &&
      request.node_id.size() <= kMaxNodeIdBytes &&
      IsValidManifestId(request.extension.extension_id) &&
      IsValidLockedVersion(request.extension.version) &&
      (request.extension.asset_manifest.empty() ||
       IsValidManifestId(request.extension.asset_manifest,
                         kMaxAssetManifestIdBytes)) &&
      IsCompatibleOutputPolicy(request.extension.output,
                               request.extension.policy_version) &&
      request.cache_key.profile_isolation != 0 &&
      request.cache_key.document_isolation != 0 &&
      request.cache_key.source_digest_present &&
      request.cache_key.options_digest_present &&
      request.cache_key.extension_id == request.extension.extension_id &&
      request.cache_key.extension_version == request.extension.version &&
      request.cache_key.policy_version == request.extension.policy_version &&
      IsKnownTheme(request.cache_key.theme);
  if (!structurally_valid ||
      impl_->requests.find(request.request_id) != impl_->requests.end()) {
    return impl_->Ephemeral(request, RuntimeRequestState::kFailed,
                            RuntimeError::kInvalidNode);
  }
  if (!GenerationsEqual(request_generations, impl_->generations)) {
    return impl_->Ephemeral(request, RuntimeRequestState::kStale,
                            RuntimeError::kStale);
  }
  if (now_ms >= request.deadline_ms) {
    return impl_->Ephemeral(request, RuntimeRequestState::kFailed,
                            RuntimeError::kTimeout);
  }
  if (request.deadline_ms - now_ms > kMaxRuntimeDeadlineMs) {
    return impl_->Ephemeral(request, RuntimeRequestState::kFailed,
                            RuntimeError::kBudgetExceeded);
  }

  impl_->PruneHistory();
  const auto cache = impl_->cache.find(request.cache_key);
  if (impl_->requests.size() >= kMaxRuntimeRequestHistory ||
      (cache == impl_->cache.end() &&
       impl_->pending_requests >= kMaxPendingRuntimeRequests)) {
    if (impl_->dropped_requests != std::numeric_limits<std::uint64_t>::max()) {
      ++impl_->dropped_requests;
    }
    return impl_->Ephemeral(request, RuntimeRequestState::kFailed,
                            RuntimeError::kCapacityExceeded);
  }

  Impl::Record record;
  record.request = std::move(request);
  record.sequence = impl_->next_sequence++;
  if (cache != impl_->cache.end()) {
    cache->second.last_access_sequence = impl_->next_sequence++;
    record.state = RuntimeRequestState::kReady;
    record.cache_hit = true;
    record.result_token = cache->second.result_token;
  } else {
    ++impl_->active_requests;
    ++impl_->pending_requests;
  }
  const auto inserted =
      impl_->requests.emplace(record.request.request_id, std::move(record));
  return impl_->SnapshotOf(inserted.first->second);
}

std::optional<RuntimeLoadResult> RuntimeSession::BeginLoad(
    std::uint64_t request_id, std::uint64_t now_ms) {
  const auto found = impl_->requests.find(request_id);
  if (found == impl_->requests.end() ||
      found->second.state != RuntimeRequestState::kQueued) {
    return std::nullopt;
  }
  Impl::Record& record = found->second;
  if (impl_->ApplyDeadline(&record, now_ms)) {
    return RuntimeLoadResult{impl_->SnapshotOf(record), nullptr};
  }
  if (impl_->concurrent_requests >= kMaxConcurrentRuntimeRenders) {
    return RuntimeLoadResult{impl_->SnapshotOf(record), nullptr};
  }
  if (impl_->catalog) {
    record.bundle =
        impl_->catalog->FindCompatible(record.request.extension.asset_manifest,
                                       record.request.extension.extension_id,
                                       record.request.extension.version);
  }
  if (!record.bundle) {
    impl_->Finish(&record, RuntimeRequestState::kFailed,
                  RuntimeError::kAssetUnavailable);
    return RuntimeLoadResult{impl_->SnapshotOf(record), nullptr};
  }
  if (impl_->pending_requests > 0) {
    --impl_->pending_requests;
  }
  ++impl_->concurrent_requests;
  record.state = RuntimeRequestState::kLoading;
  return RuntimeLoadResult{impl_->SnapshotOf(record), record.bundle};
}

bool RuntimeSession::BeginRendering(std::uint64_t request_id,
                                    std::uint64_t now_ms) {
  const auto found = impl_->requests.find(request_id);
  if (found == impl_->requests.end() ||
      found->second.state != RuntimeRequestState::kLoading ||
      impl_->ApplyDeadline(&found->second, now_ms)) {
    return false;
  }
  found->second.state = RuntimeRequestState::kRendering;
  return true;
}

bool RuntimeSession::Complete(std::uint64_t request_id,
                              std::uint64_t result_token,
                              std::size_t retained_bytes,
                              std::uint64_t now_ms) {
  const auto found = impl_->requests.find(request_id);
  if (found == impl_->requests.end() ||
      found->second.state != RuntimeRequestState::kRendering ||
      impl_->ApplyDeadline(&found->second, now_ms)) {
    return false;
  }
  Impl::Record& record = found->second;
  if (result_token == 0 || retained_bytes > kMaxCachedResultBytes) {
    impl_->Finish(&record, RuntimeRequestState::kFailed,
                  RuntimeError::kBudgetExceeded);
    return false;
  }
  impl_->Finish(&record, RuntimeRequestState::kReady, RuntimeError::kNone);
  record.result_token = result_token;
  impl_->InsertCache(record.request.cache_key, result_token, retained_bytes);
  return true;
}

bool RuntimeSession::Fail(std::uint64_t request_id, RuntimeError error,
                          std::uint64_t now_ms) {
  const auto found = impl_->requests.find(request_id);
  if (found == impl_->requests.end() || !IsActive(found->second.state) ||
      !IsAllowedFailure(error) ||
      impl_->ApplyDeadline(&found->second, now_ms)) {
    return false;
  }
  impl_->Finish(&found->second, RuntimeRequestState::kFailed, error);
  return true;
}

bool RuntimeSession::Cancel(std::uint64_t request_id) {
  const auto found = impl_->requests.find(request_id);
  if (found == impl_->requests.end()) {
    return false;
  }
  if (found->second.state == RuntimeRequestState::kCancelled) {
    return true;
  }
  if (!IsActive(found->second.state)) {
    return false;
  }
  impl_->Finish(&found->second, RuntimeRequestState::kCancelled,
                RuntimeError::kCancelled);
  return true;
}

std::optional<RuntimeRequestSnapshot> RuntimeSession::Snapshot(
    std::uint64_t request_id) const {
  const auto found = impl_->requests.find(request_id);
  if (found == impl_->requests.end()) {
    return std::nullopt;
  }
  return impl_->SnapshotOf(found->second);
}

void RuntimeSession::AdvanceGenerations(RuntimeGenerations generations) {
  if (GenerationsEqual(generations, impl_->generations)) {
    return;
  }
  impl_->generations = generations;
  for (auto& item : impl_->requests) {
    if (IsActive(item.second.state)) {
      impl_->Finish(&item.second, RuntimeRequestState::kStale,
                    RuntimeError::kStale);
    } else if (item.second.state == RuntimeRequestState::kReady) {
      item.second.state = RuntimeRequestState::kStale;
      item.second.error = RuntimeError::kStale;
      item.second.cache_hit = false;
      item.second.result_token = 0;
      item.second.bundle.reset();
    }
  }
  impl_->ClearCache();
}

void RuntimeSession::OnMemoryPressure() { impl_->ClearCache(); }

void RuntimeSession::Shutdown() {
  if (impl_->shutdown) {
    return;
  }
  impl_->shutdown = true;
  for (auto& item : impl_->requests) {
    if (IsActive(item.second.state)) {
      impl_->Finish(&item.second, RuntimeRequestState::kCancelled,
                    RuntimeError::kCancelled);
    }
  }
  impl_->requests.clear();
  impl_->ClearCache();
  impl_->catalog.reset();
  impl_->active_requests = 0;
  impl_->pending_requests = 0;
  impl_->concurrent_requests = 0;
}

RuntimeGenerations RuntimeSession::generations() const noexcept {
  return impl_->generations;
}

std::size_t RuntimeSession::active_request_count() const noexcept {
  return impl_->active_requests;
}

std::size_t RuntimeSession::pending_request_count() const noexcept {
  return impl_->pending_requests;
}

std::size_t RuntimeSession::concurrent_request_count() const noexcept {
  return impl_->concurrent_requests;
}

std::size_t RuntimeSession::request_count() const noexcept {
  return impl_->requests.size();
}

std::size_t RuntimeSession::cache_entry_count() const noexcept {
  return impl_->cache.size();
}

std::size_t RuntimeSession::cache_bytes() const noexcept {
  return impl_->cache_bytes;
}

std::uint64_t RuntimeSession::dropped_request_count() const noexcept {
  return impl_->dropped_requests;
}

bool RuntimeSession::is_shutdown() const noexcept { return impl_->shutdown; }

}  // namespace crayon::browser_markdown_runtime
