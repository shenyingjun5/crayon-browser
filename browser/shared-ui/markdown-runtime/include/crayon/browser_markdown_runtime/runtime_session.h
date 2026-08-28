// MRT-04: owner-thread request lifecycle and session-only metadata cache.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "crayon/browser_markdown_runtime/extension_registry.h"
#include "crayon/browser_markdown_runtime/runtime_assets.h"

namespace crayon::browser_markdown_runtime {

inline constexpr std::size_t kMaxConcurrentRuntimeRenders = 4;
inline constexpr std::size_t kMaxPendingRuntimeRequests = 16;
inline constexpr std::size_t kMaxRuntimeRequestHistory = 256;
inline constexpr std::size_t kMaxRuntimeCacheEntries = 128;
inline constexpr std::size_t kMaxCachedResultBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxRuntimeCacheBytes = 16 * 1024 * 1024;
inline constexpr std::uint64_t kMaxRuntimeDeadlineMs = 30 * 1000;

using RuntimeDigest = std::array<std::uint8_t, 32>;

enum class RuntimeTheme { kUnknown = 0, kLight, kDark };

enum class RuntimeRequestState {
  kUnrequested = 0,
  kQueued,
  kLoading,
  kRendering,
  kReady,
  kFailed,
  kCancelled,
  kStale,
};

enum class RuntimeError {
  kNone = 0,
  kInvalidNode,
  kUnknownKind,
  kDisabled,
  kRegistryConflict,
  kBudgetExceeded,
  kCapacityExceeded,
  kAssetUnavailable,
  kLoadFailed,
  kRenderFailed,
  kTimeout,
  kCancelled,
  kStale,
  kOutputRejected,
};

struct RuntimeGenerations final {
  std::uint64_t document = 0;
  std::uint64_t source = 0;
  std::uint64_t extension = 0;
};

struct RuntimeCacheKey final {
  std::uint64_t profile_isolation = 0;
  std::uint64_t document_isolation = 0;
  std::string extension_id;
  std::string extension_version;
  bool source_digest_present = false;
  RuntimeDigest source_digest{};
  RuntimeTheme theme = RuntimeTheme::kUnknown;
  bool options_digest_present = false;
  RuntimeDigest options_digest{};
  ExtensionPolicyVersion policy_version = ExtensionPolicyVersion::kUnknown;
};

struct RuntimeRequest final {
  std::uint64_t request_id = 0;
  std::string node_id;
  ExtensionDescriptor extension;
  RuntimeCacheKey cache_key;
  std::uint64_t deadline_ms = 0;
};

struct RuntimeRequestSnapshot final {
  std::uint64_t request_id = 0;
  RuntimeRequestState state = RuntimeRequestState::kUnrequested;
  RuntimeError error = RuntimeError::kNone;
  RuntimeGenerations generations;
  bool cache_hit = false;
  std::uint64_t result_token = 0;
};

struct RuntimeLoadResult final {
  RuntimeRequestSnapshot request;
  std::shared_ptr<const RuntimeAssetBundle> bundle;
};

class RuntimeSession final {
 public:
  /// The session is deliberately owner-thread confined. It performs no IO,
  /// starts no worker and invokes no callback; platform/adapter code drives
  /// transitions and may post results back to the owner thread.
  RuntimeSession(RuntimeGenerations generations,
                 std::shared_ptr<const RuntimeAssetCatalog> catalog);
  ~RuntimeSession();
  RuntimeSession(const RuntimeSession&) = delete;
  RuntimeSession& operator=(const RuntimeSession&) = delete;

  /// Queues a generation-bound request or returns an immediate cache hit/error.
  RuntimeRequestSnapshot Queue(RuntimeRequest request, std::uint64_t now_ms);
  /// Acquires the exact compiled bundle and moves queued -> loading. A null
  /// bundle with queued state means all concurrent slots are occupied; a null
  /// bundle with failed state carries timeout/asset_unavailable.
  std::optional<RuntimeLoadResult> BeginLoad(std::uint64_t request_id,
                                             std::uint64_t now_ms);
  /// Records adapter load completion and moves loading -> rendering.
  bool BeginRendering(std::uint64_t request_id, std::uint64_t now_ms);
  /// Publishes only an opaque Browser-owned result token, never renderer
  /// payload. retained_bytes is mandatory bounded cache accounting.
  bool Complete(std::uint64_t request_id, std::uint64_t result_token,
                std::size_t retained_bytes, std::uint64_t now_ms);
  bool Fail(std::uint64_t request_id, RuntimeError error, std::uint64_t now_ms);
  bool Cancel(std::uint64_t request_id);
  std::optional<RuntimeRequestSnapshot> Snapshot(
      std::uint64_t request_id) const;

  /// Invalidates active and ready work and clears all cache entries.
  void AdvanceGenerations(RuntimeGenerations generations);
  void OnMemoryPressure();
  /// Idempotent cancel -> detach(no callbacks) -> clear resources.
  void Shutdown();

  RuntimeGenerations generations() const noexcept;
  std::size_t active_request_count() const noexcept;
  std::size_t pending_request_count() const noexcept;
  std::size_t concurrent_request_count() const noexcept;
  std::size_t request_count() const noexcept;
  std::size_t cache_entry_count() const noexcept;
  std::size_t cache_bytes() const noexcept;
  std::uint64_t dropped_request_count() const noexcept;
  bool is_shutdown() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crayon::browser_markdown_runtime
