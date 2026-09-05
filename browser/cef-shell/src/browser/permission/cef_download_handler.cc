#include "browser/permission/cef_download_handler.h"

#include <algorithm>

#include "browser/permission/site_origin.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::permission {

CefDownloadHandlerAdapter::CefDownloadHandlerAdapter(
    PermissionStore *store, CefDownloadObserver *observer)
    : store_(store), observer_(observer) {
  // Passive adapter: no CEF state is touched here, and construction runs
  // before CefInitialize on the main thread; thread checks live in the
  // callback methods.
}

bool CefDownloadHandlerAdapter::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
    const CefString &suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(suggested_name);

  // The decision is always ours (handled); returning false would fall
  // back to CEF default handling and bypass the permission store.
  if (!browser || !download_item || !callback || !store_) {
    return true; // fail closed: cancel by not invoking the callback
  }

  CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
  if (!main_frame) {
    return true; // fail closed
  }

  const std::optional<std::string> origin =
      ExtractSiteOrigin(main_frame->GetURL().ToString());
  if (!origin.has_value() ||
      store_->Query(*origin, PermissionKind::kDownload) ==
          PermissionDecision::kDeny) {
    // Cancel the download by not invoking the callback.
    return true;
  }

  if (!observer_) {
    callback->Continue(CefString(), true);
    return true;
  }
  const std::uint64_t id = download_item->GetId();
  const auto decision = observer_->OnDownloadStarting(
      id, suggested_name.ToString(), download_item->GetURL().ToString());
  if (decision.generation == 0) {
    return true;
  }
  if (decision.kind == DownloadStartKind::kAccept &&
      !decision.target_path.empty()) {
    generations_[id] = decision.generation;
    callback->Continue(decision.target_path, false);
  } else if (decision.kind == DownloadStartKind::kPending) {
    generations_[id] = decision.generation;
    pending_[id] = callback;
  }
  return true;
}

void CefDownloadHandlerAdapter::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  static_cast<void>(browser);
  if (!download_item || !callback)
    return;
  const std::uint64_t id = download_item->GetId();
  const auto generation = generations_.find(id);
  if (observer_ && generation == generations_.end())
    return;
  controls_[id] = callback;
  if (observer_) {
    observer_->OnDownloadProgress(
        {id, generation->second,
         static_cast<std::uint64_t>(
             (std::max)(download_item->GetReceivedBytes(), int64_t{0})),
         static_cast<std::uint64_t>(
             (std::max)(download_item->GetTotalBytes(), int64_t{0})),
         download_item->IsComplete(), download_item->IsCanceled(),
         download_item->IsInProgress()});
  }
  const bool interrupted =
      !download_item->IsComplete() && !download_item->IsCanceled() &&
      !download_item->IsInProgress() && pending_.count(id) == 0;
  if (download_item->IsComplete() || download_item->IsCanceled() ||
      interrupted) {
    controls_.erase(id);
    pending_.erase(id);
    generations_.erase(id);
  }
}

bool CefDownloadHandlerAdapter::ConfirmPending(std::uint64_t download_id,
                                               const std::string &target_path) {
  CEF_REQUIRE_UI_THREAD();
  auto found = pending_.find(download_id);
  if (found == pending_.end() || target_path.empty())
    return false;
  auto callback = std::move(found->second);
  pending_.erase(found);
  callback->Continue(target_path, false);
  return true;
}

bool CefDownloadHandlerAdapter::DiscardPending(std::uint64_t download_id) {
  CEF_REQUIRE_UI_THREAD();
  if (pending_.erase(download_id) != 1)
    return false;
  generations_.erase(download_id);
  return true;
}

bool CefDownloadHandlerAdapter::Pause(std::uint64_t download_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = controls_.find(download_id);
  if (found == controls_.end())
    return false;
  found->second->Pause();
  return true;
}

bool CefDownloadHandlerAdapter::Resume(std::uint64_t download_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = controls_.find(download_id);
  if (found == controls_.end())
    return false;
  found->second->Resume();
  return true;
}

bool CefDownloadHandlerAdapter::Cancel(std::uint64_t download_id) {
  CEF_REQUIRE_UI_THREAD();
  auto found = controls_.find(download_id);
  if (found == controls_.end())
    return false;
  found->second->Cancel();
  controls_.erase(found);
  pending_.erase(download_id);
  generations_.erase(download_id);
  return true;
}

void CefDownloadHandlerAdapter::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  pending_.clear();
  controls_.clear();
  generations_.clear();
  observer_ = nullptr;
}

} // namespace crayon::browser::cef_shell::permission
