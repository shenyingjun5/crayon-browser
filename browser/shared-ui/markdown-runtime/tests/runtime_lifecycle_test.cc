// MRT-04 / MR-003: fixed assets, lazy acquire, cache and lifecycle tests.
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "crayon/browser_markdown_runtime/runtime_session.h"

namespace {

namespace runtime = crayon::browser_markdown_runtime;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

runtime::RuntimeAssetBundle Bundle(std::string manifest_id = "diagram-v1") {
  runtime::RuntimeAssetBundle bundle;
  bundle.manifest_id = std::move(manifest_id);
  bundle.extension_id = "diagram";
  bundle.extension_version = "1.2.3";
  bundle.entry_resource_id = "entry";
  bundle.resources = {
      {"entry", runtime::RuntimeAssetContentType::kJavaScript,
       "export function render(){}"},
      {"style", runtime::RuntimeAssetContentType::kCss,
       ".diagram{max-width:100%}"},
  };
  return bundle;
}

std::shared_ptr<const runtime::RuntimeAssetCatalog> Catalog() {
  return runtime::BuildRuntimeAssetCatalog({Bundle()}).catalog;
}

runtime::ExtensionDescriptor Descriptor() {
  runtime::ExtensionDescriptor descriptor;
  descriptor.extension_id = "diagram";
  descriptor.version = "1.2.3";
  descriptor.output = runtime::ExtensionOutputKind::kSvg;
  descriptor.asset_manifest = "diagram-v1";
  descriptor.policy_version = runtime::ExtensionPolicyVersion::kSvgV1;
  descriptor.document_generation = 1;
  descriptor.source_revision = 2;
  descriptor.extension_generation = 3;
  return descriptor;
}

runtime::RuntimeRequest Request(std::uint64_t request_id) {
  runtime::RuntimeRequest request;
  request.request_id = request_id;
  request.node_id = "node-" + std::to_string(request_id);
  request.extension = Descriptor();
  request.cache_key.profile_isolation = 41;
  request.cache_key.document_isolation = 43;
  request.cache_key.extension_id = "diagram";
  request.cache_key.extension_version = "1.2.3";
  request.cache_key.source_digest_present = true;
  request.cache_key.source_digest[0] = 7;
  request.cache_key.theme = runtime::RuntimeTheme::kLight;
  request.cache_key.options_digest_present = true;
  request.cache_key.options_digest[0] = 11;
  request.cache_key.policy_version = runtime::ExtensionPolicyVersion::kSvgV1;
  request.deadline_ms = 1000;
  return request;
}

bool CatalogIsClosedAndBounded() {
  auto built = runtime::BuildRuntimeAssetCatalog({Bundle()});
  CHECK(built.status == runtime::AssetCatalogBuildStatus::kReady);
  CHECK(built.catalog != nullptr);
  CHECK(built.catalog->bundle_count() == 1);
  CHECK(built.catalog->total_bytes() > 0);
  const auto found =
      built.catalog->FindCompatible("diagram-v1", "diagram", "1.2.3");
  CHECK(found != nullptr && found->resources.size() == 2);
  CHECK(built.catalog->FindCompatible("diagram-v1", "other", "1.2.3") ==
        nullptr);

  auto duplicate_resource = Bundle();
  duplicate_resource.resources.push_back(duplicate_resource.resources[0]);
  auto missing_entry = Bundle();
  missing_entry.entry_resource_id = "missing";
  auto path_id = Bundle("../runtime");
  auto tag_version = Bundle();
  tag_version.extension_version = "latest";
  auto unknown_type = Bundle();
  unknown_type.resources[0].content_type =
      static_cast<runtime::RuntimeAssetContentType>(99);
  auto empty_bytes = Bundle();
  empty_bytes.resources[0].bytes.clear();
  auto wrong_entry_type = Bundle();
  wrong_entry_type.resources[0].content_type =
      runtime::RuntimeAssetContentType::kCss;
  for (auto invalid : {duplicate_resource, missing_entry, path_id, tag_version,
                       unknown_type, empty_bytes, wrong_entry_type}) {
    const auto rejected =
        runtime::BuildRuntimeAssetCatalog({std::move(invalid)});
    CHECK(rejected.status == runtime::AssetCatalogBuildStatus::kInvalidCatalog);
    CHECK(rejected.catalog == nullptr);
  }

  std::vector<runtime::RuntimeAssetBundle> too_many;
  for (std::size_t i = 0; i < runtime::kMaxAssetBundles + 1; ++i) {
    too_many.push_back(Bundle("bundle-" + std::to_string(i)));
  }
  const auto over = runtime::BuildRuntimeAssetCatalog(std::move(too_many));
  CHECK(over.status == runtime::AssetCatalogBuildStatus::kBudgetExceeded);
  CHECK(over.catalog == nullptr);

  auto too_many_resources = Bundle();
  too_many_resources.resources.clear();
  for (std::size_t i = 0; i < runtime::kMaxAssetsPerBundle + 1; ++i) {
    too_many_resources.resources.push_back(
        {"asset-" + std::to_string(i),
         runtime::RuntimeAssetContentType::kJavaScript, "x"});
  }
  too_many_resources.entry_resource_id = "asset-0";
  const auto resource_over =
      runtime::BuildRuntimeAssetCatalog({std::move(too_many_resources)});
  CHECK(resource_over.status ==
        runtime::AssetCatalogBuildStatus::kBudgetExceeded);
  CHECK(resource_over.catalog == nullptr);
  return true;
}

bool MissingAssetsFailOnlyWhenLoadBegins() {
  const auto empty = runtime::BuildRuntimeAssetCatalog({});
  runtime::RuntimeSession session({1, 2, 3}, empty.catalog);
  const auto queued = session.Queue(Request(1), 10);
  CHECK(queued.state == runtime::RuntimeRequestState::kQueued);
  CHECK(queued.error == runtime::RuntimeError::kNone);
  const auto load = session.BeginLoad(1, 11);
  CHECK(load.has_value());
  CHECK(load->bundle == nullptr);
  CHECK(load->request.state == runtime::RuntimeRequestState::kFailed);
  CHECK(load->request.error == runtime::RuntimeError::kAssetUnavailable);
  return true;
}

bool NormalLifecycleAndCacheHit() {
  runtime::RuntimeSession session({1, 2, 3}, Catalog());
  auto queued = session.Queue(Request(1), 10);
  CHECK(queued.state == runtime::RuntimeRequestState::kQueued);
  CHECK(session.active_request_count() == 1);
  auto load = session.BeginLoad(1, 20);
  CHECK(load.has_value() && load->bundle != nullptr);
  CHECK(load->request.state == runtime::RuntimeRequestState::kLoading);
  CHECK(session.BeginRendering(1, 30));
  CHECK(session.Snapshot(1)->state == runtime::RuntimeRequestState::kRendering);
  CHECK(session.Complete(1, 7001, 1024, 40));
  CHECK(session.Snapshot(1)->state == runtime::RuntimeRequestState::kReady);
  CHECK(session.active_request_count() == 0);
  CHECK(session.cache_entry_count() == 1);
  CHECK(session.cache_bytes() == 1024);

  auto repeated = Request(2);
  repeated.node_id = "node-cache-hit";
  const auto hit = session.Queue(std::move(repeated), 50);
  CHECK(hit.state == runtime::RuntimeRequestState::kReady);
  CHECK(hit.cache_hit && hit.result_token == 7001);
  CHECK(session.active_request_count() == 0);

  auto isolated = Request(3);
  isolated.cache_key.profile_isolation = 99;
  CHECK(session.Queue(std::move(isolated), 50).state ==
        runtime::RuntimeRequestState::kQueued);
  return true;
}

bool ErrorsTransitionsDeadlineAndCancelAreClosed() {
  runtime::RuntimeSession session({1, 2, 3}, Catalog());
  CHECK(!session.BeginRendering(404, 1));
  CHECK(!session.Complete(404, 1, 1, 1));
  CHECK(!session.Fail(404, runtime::RuntimeError::kRenderFailed, 1));

  CHECK(session.Queue(Request(10), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(!session.BeginRendering(10, 11));
  CHECK(session.Snapshot(10)->state == runtime::RuntimeRequestState::kQueued);
  CHECK(session.Cancel(10));
  CHECK(session.Cancel(10));
  CHECK(session.Snapshot(10)->error == runtime::RuntimeError::kCancelled);
  CHECK(!session.BeginLoad(10, 12).has_value());

  auto expired = Request(11);
  expired.deadline_ms = 20;
  CHECK(session.Queue(std::move(expired), 21).error ==
        runtime::RuntimeError::kTimeout);

  CHECK(session.Queue(Request(12), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.BeginLoad(12, 20)->bundle != nullptr);
  CHECK(session.BeginRendering(12, 30));
  CHECK(session.Fail(12, runtime::RuntimeError::kRenderFailed, 40));
  CHECK(session.Snapshot(12)->state == runtime::RuntimeRequestState::kFailed);
  CHECK(!session.Fail(12, runtime::RuntimeError::kLoadFailed, 41));

  auto load_timeout = Request(13);
  load_timeout.deadline_ms = 20;
  CHECK(session.Queue(std::move(load_timeout), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  const auto timed_out_load = session.BeginLoad(13, 20);
  CHECK(timed_out_load.has_value());
  CHECK(timed_out_load->request.error == runtime::RuntimeError::kTimeout);

  auto render_timeout = Request(14);
  render_timeout.deadline_ms = 30;
  CHECK(session.Queue(std::move(render_timeout), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.BeginLoad(14, 11)->bundle != nullptr);
  CHECK(session.BeginRendering(14, 12));
  CHECK(!session.Complete(14, 1400, 1, 30));
  CHECK(session.Snapshot(14)->error == runtime::RuntimeError::kTimeout);

  CHECK(session.Queue(Request(15), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.BeginLoad(15, 11)->bundle != nullptr);
  CHECK(session.BeginRendering(15, 12));
  CHECK(!session.Complete(15, 1500, runtime::kMaxCachedResultBytes + 1, 13));
  CHECK(session.Snapshot(15)->error == runtime::RuntimeError::kBudgetExceeded);

  CHECK(session.Queue(Request(16), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.Queue(Request(17), 10).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.BeginLoad(16, 11)->bundle != nullptr);
  CHECK(session.BeginLoad(17, 11)->bundle != nullptr);
  CHECK(session.Fail(16, runtime::RuntimeError::kLoadFailed, 12));
  CHECK(session.Snapshot(17)->state == runtime::RuntimeRequestState::kLoading);
  return true;
}

bool InvalidAndStaleRequestsNeverLoad() {
  runtime::RuntimeSession session({1, 2, 3}, Catalog());
  auto invalid = Request(0);
  CHECK(session.Queue(std::move(invalid), 1).error ==
        runtime::RuntimeError::kInvalidNode);
  invalid = Request(20);
  invalid.node_id.clear();
  CHECK(session.Queue(std::move(invalid), 1).error ==
        runtime::RuntimeError::kInvalidNode);
  invalid = Request(21);
  invalid.cache_key.extension_version = "9.9.9";
  CHECK(session.Queue(std::move(invalid), 1).error ==
        runtime::RuntimeError::kInvalidNode);
  invalid = Request(24);
  invalid.cache_key.source_digest_present = false;
  CHECK(session.Queue(std::move(invalid), 1).error ==
        runtime::RuntimeError::kInvalidNode);
  invalid = Request(25);
  invalid.deadline_ms = 1 + runtime::kMaxRuntimeDeadlineMs + 1;
  CHECK(session.Queue(std::move(invalid), 1).error ==
        runtime::RuntimeError::kBudgetExceeded);
  invalid = Request(22);
  invalid.extension.source_revision = 8;
  CHECK(session.Queue(std::move(invalid), 1).state ==
        runtime::RuntimeRequestState::kStale);

  CHECK(session.Queue(Request(23), 1).state ==
        runtime::RuntimeRequestState::kQueued);
  session.AdvanceGenerations({2, 3, 4});
  CHECK(session.Snapshot(23)->state == runtime::RuntimeRequestState::kStale);
  CHECK(session.Snapshot(23)->error == runtime::RuntimeError::kStale);
  CHECK(session.active_request_count() == 0);
  return true;
}

bool CapacityHistoryAndCacheStayBounded() {
  runtime::RuntimeSession session({1, 2, 3}, Catalog());
  for (std::size_t i = 0; i < runtime::kMaxPendingRuntimeRequests; ++i) {
    CHECK(session.Queue(Request(100 + i), 1).state ==
          runtime::RuntimeRequestState::kQueued);
  }
  const auto full =
      session.Queue(Request(100 + runtime::kMaxPendingRuntimeRequests), 1);
  CHECK(full.error == runtime::RuntimeError::kCapacityExceeded);
  CHECK(session.request_count() == runtime::kMaxPendingRuntimeRequests);
  CHECK(session.dropped_request_count() == 1);
  for (std::size_t i = 0; i < runtime::kMaxPendingRuntimeRequests; ++i) {
    CHECK(session.Cancel(100 + i));
  }

  for (std::size_t i = 0; i < runtime::kMaxConcurrentRuntimeRenders + 1; ++i) {
    CHECK(session.Queue(Request(500 + i), 1).state ==
          runtime::RuntimeRequestState::kQueued);
  }
  for (std::size_t i = 0; i < runtime::kMaxConcurrentRuntimeRenders; ++i) {
    CHECK(session.BeginLoad(500 + i, 2)->bundle != nullptr);
  }
  const auto waiting =
      session.BeginLoad(500 + runtime::kMaxConcurrentRuntimeRenders, 2);
  CHECK(waiting.has_value() && waiting->bundle == nullptr);
  CHECK(waiting->request.state == runtime::RuntimeRequestState::kQueued);
  CHECK(session.concurrent_request_count() ==
        runtime::kMaxConcurrentRuntimeRenders);
  CHECK(session.pending_request_count() == 1);
  for (std::size_t i = 0; i < runtime::kMaxConcurrentRuntimeRenders + 1; ++i) {
    CHECK(session.Cancel(500 + i));
  }

  for (std::size_t i = 0; i < runtime::kMaxRuntimeRequestHistory + 50; ++i) {
    const std::uint64_t id = 1000 + i;
    CHECK(session.Queue(Request(id), 1).state ==
          runtime::RuntimeRequestState::kQueued);
    CHECK(session.Cancel(id));
  }
  CHECK(session.request_count() <= runtime::kMaxRuntimeRequestHistory);

  runtime::RuntimeSession cache_session({1, 2, 3}, Catalog());
  for (std::size_t i = 0; i < runtime::kMaxRuntimeCacheEntries + 1; ++i) {
    auto request = Request(5000 + i);
    request.cache_key.source_digest[1] = static_cast<std::uint8_t>(i);
    request.cache_key.source_digest[2] = static_cast<std::uint8_t>(i / 256);
    CHECK(cache_session.Queue(std::move(request), 1).state ==
          runtime::RuntimeRequestState::kQueued);
    CHECK(cache_session.BeginLoad(5000 + i, 2)->bundle != nullptr);
    CHECK(cache_session.BeginRendering(5000 + i, 3));
    CHECK(cache_session.Complete(5000 + i, 9000 + i, 1, 4));
  }
  CHECK(cache_session.cache_entry_count() == runtime::kMaxRuntimeCacheEntries);
  CHECK(cache_session.cache_bytes() == runtime::kMaxRuntimeCacheEntries);
  auto first = Request(90000);
  first.cache_key.source_digest[1] = 0;
  first.cache_key.source_digest[2] = 0;
  CHECK(cache_session.Queue(std::move(first), 5).state ==
        runtime::RuntimeRequestState::kQueued);
  return true;
}

bool MemoryPressureGenerationAndShutdownCleanUp() {
  runtime::RuntimeSession session({1, 2, 3}, Catalog());
  CHECK(session.Queue(Request(1), 1).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.BeginLoad(1, 2)->bundle != nullptr);
  CHECK(session.BeginRendering(1, 3));
  CHECK(session.Complete(1, 88, 128, 4));
  CHECK(session.cache_entry_count() == 1);
  session.AdvanceGenerations({1, 2, 4});
  CHECK(session.Snapshot(1)->state == runtime::RuntimeRequestState::kStale);
  CHECK(session.Snapshot(1)->result_token == 0);
  CHECK(session.cache_entry_count() == 0);

  session.AdvanceGenerations({1, 2, 3});
  CHECK(session.Queue(Request(10), 1).state ==
        runtime::RuntimeRequestState::kQueued);
  CHECK(session.BeginLoad(10, 2)->bundle != nullptr);
  CHECK(session.BeginRendering(10, 3));
  CHECK(session.Complete(10, 89, 128, 4));
  session.OnMemoryPressure();
  CHECK(session.cache_entry_count() == 0 && session.cache_bytes() == 0);

  CHECK(session.Queue(Request(2), 5).state ==
        runtime::RuntimeRequestState::kQueued);
  session.Shutdown();
  session.Shutdown();
  CHECK(session.is_shutdown());
  CHECK(session.active_request_count() == 0);
  CHECK(session.request_count() == 0);
  CHECK(session.cache_entry_count() == 0);
  CHECK(session.Queue(Request(3), 6).error ==
        runtime::RuntimeError::kCancelled);
  return true;
}

}  // namespace

int main() {
  const bool ok = CatalogIsClosedAndBounded() &&
                  MissingAssetsFailOnlyWhenLoadBegins() &&
                  NormalLifecycleAndCacheHit() &&
                  ErrorsTransitionsDeadlineAndCancelAreClosed() &&
                  InvalidAndStaleRequestsNeverLoad() &&
                  CapacityHistoryAndCacheStayBounded() &&
                  MemoryPressureGenerationAndShutdownCleanUp();
  if (!ok) return EXIT_FAILURE;
  std::cout << "runtime_lifecycle_test passed\n";
  return EXIT_SUCCESS;
}
