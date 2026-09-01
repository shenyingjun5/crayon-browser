#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "browser/window/popup_target.h"
#include "browser/window/tab_model.h"

namespace {

using crayon::browser::cef_shell::window::kDefaultZoomFactor;
using crayon::browser::cef_shell::window::kMaximumTabsPerWindow;
using crayon::browser::cef_shell::window::kMaximumWindowZoomFactor;
using crayon::browser::cef_shell::window::kMinimumWindowZoomFactor;
using crayon::browser::cef_shell::window::TabId;
using crayon::browser::cef_shell::window::TabLifecycle;
using crayon::browser::cef_shell::window::TabModel;
using crayon::browser::cef_shell::window::TabSnapshot;

void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const TabSnapshot& RequireTab(const TabModel& model, TabId id) {
  const TabSnapshot* tab = model.Find(id);
  Check(tab != nullptr, "tab must exist");
  return *tab;
}

void CreateActivateAndOrder() {
  TabModel model;
  const TabId first = model.CreateTab().value();
  Check(model.active_tab() == first, "first tab becomes active");
  const TabId second = model.CreateTab().value();
  const TabId third = model.CreateTab().value();
  Check(model.active_tab() == third, "new tab becomes active");
  Check(model.size() == 3, "three tabs tracked");

  const std::vector<TabId> order = model.ordered_tabs();
  Check(order.size() == 3 && order[0] == first && order[1] == second &&
            order[2] == third,
        "tab order matches creation order");

  Check(model.Activate(first), "activate existing tab");
  Check(model.active_tab() == first, "activation switches active tab");
  Check(!model.Activate(9999), "activate unknown tab fails");
  Check(model.active_tab() == first,
        "failed activation keeps previous active tab");
}

void CapacityLimit() {
  TabModel model;
  for (std::size_t i = 0; i < kMaximumTabsPerWindow; ++i) {
    Check(model.CreateTab().has_value(), "tab within capacity is created");
  }
  Check(!model.CreateTab().has_value(), "tab beyond capacity is rejected");
  Check(model.size() == kMaximumTabsPerWindow, "size stays at capacity");
}

void DuplicateAndStaleClose() {
  TabModel model;
  const TabId first = model.CreateTab().value();
  const TabId second = model.CreateTab().value();
  Check(model.BindBrowser(first, 11), "bind first browser");
  Check(model.BindBrowser(second, 12), "bind second browser");

  Check(model.RequestClose(first), "first close request accepted");
  Check(RequireTab(model, first).lifecycle == TabLifecycle::kClosing,
        "close request marks tab closing");
  Check(model.RequestClose(first), "repeated close request is idempotent");
  Check(model.size() == 2, "close request does not detach synchronously");

  Check(!model.Activate(first), "closing tab cannot be activated");
  Check(!model.SetZoom(first, 2.0), "closing tab rejects zoom");
  Check(!model.UpdateLoading(11, true, false, false),
        "stale browser loading callback is rejected");
  Check(!model.UpdateAddress(11, "about:blank"),
        "stale browser address callback is rejected");
  Check(!model.BeginNavigation(11),
        "stale browser navigation callback is rejected");

  Check(model.DetachBrowser(11), "detach removes closing tab");
  Check(model.size() == 1, "closed tab removed from model");
  Check(!model.DetachBrowser(11), "repeated detach reports unknown browser");
  Check(model.active_tab() == second, "active tab moves to replacement");
}

void CloseLastTabEmptiesModel() {
  TabModel model;
  const TabId only = model.CreateTab().value();
  Check(model.BindBrowser(only, 21), "bind only tab");
  Check(model.RequestClose(only), "close only tab");
  Check(model.DetachBrowser(21), "detach only tab");
  Check(model.empty(), "model is empty after last tab closes");
  Check(!model.active_tab().has_value(),
        "active tab cleared after last tab closes");
}

void ActiveReplacementPrefersNextThenPrevious() {
  TabModel model;
  const TabId first = model.CreateTab().value();
  const TabId second = model.CreateTab().value();
  const TabId third = model.CreateTab().value();
  Check(model.BindBrowser(first, 31) && model.BindBrowser(second, 32) &&
            model.BindBrowser(third, 33),
        "bind all tabs");

  Check(model.Activate(second), "activate middle tab");
  Check(model.DetachBrowser(32), "detach middle tab");
  Check(model.active_tab() == third,
        "closing middle tab activates next tab");

  Check(model.DetachBrowser(33), "detach last tab");
  Check(model.active_tab() == first,
        "closing last tab activates previous tab");
}

void LoadingHistoryAndAddress() {
  TabModel model;
  const TabId tab_id = model.CreateTab().value();
  Check(model.BindBrowser(tab_id, 41), "bind browser");

  Check(model.UpdateAddress(41, "about:blank"), "address update accepted");
  Check(model.UpdateLoading(41, true, false, false), "loading start accepted");
  const TabSnapshot& loading = RequireTab(model, tab_id);
  Check(loading.loading && !loading.can_go_back && !loading.can_go_forward,
        "loading state recorded");
  Check(loading.url == "about:blank", "address recorded");

  Check(model.UpdateLoading(41, false, true, false), "loading stop accepted");
  const TabSnapshot& loaded = RequireTab(model, tab_id);
  Check(!loaded.loading && loaded.can_go_back && !loaded.can_go_forward,
        "history state recorded");
}

void NavigationGeneration() {
  TabModel model;
  const TabId tab_id = model.CreateTab().value();
  Check(model.BindBrowser(tab_id, 51), "bind browser");
  Check(RequireTab(model, tab_id).navigation_generation == 0,
        "generation starts at zero");
  Check(model.BeginNavigation(51), "navigation accepted");
  Check(model.BeginNavigation(51), "second navigation accepted");
  Check(RequireTab(model, tab_id).navigation_generation == 2,
        "generation increments per navigation");
  Check(!model.BeginNavigation(9999), "unknown browser navigation rejected");
}

void ZoomBounds() {
  TabModel model;
  const TabId tab_id = model.CreateTab().value();
  Check(RequireTab(model, tab_id).zoom_factor == kDefaultZoomFactor,
        "default zoom factor applied");
  Check(model.SetZoom(tab_id, kMinimumWindowZoomFactor),
        "minimum zoom accepted");
  Check(model.SetZoom(tab_id, kMaximumWindowZoomFactor),
        "maximum zoom accepted");
  Check(!model.SetZoom(tab_id, kMinimumWindowZoomFactor / 2.0),
        "below minimum zoom rejected");
  Check(!model.SetZoom(tab_id, kMaximumWindowZoomFactor * 2.0),
        "above maximum zoom rejected");
  Check(!model.SetZoom(tab_id, std::numeric_limits<double>::quiet_NaN()),
        "NaN zoom rejected");
  Check(!model.SetZoom(tab_id, std::numeric_limits<double>::infinity()),
        "infinite zoom rejected");
  Check(RequireTab(model, tab_id).zoom_factor == kMaximumWindowZoomFactor,
        "rejected zoom leaves previous factor");
  Check(!model.SetZoom(9999, 2.0), "unknown tab zoom rejected");
}

void CrashDetachAndRecovery() {
  TabModel model;
  const TabId tab_id = model.CreateTab().value();
  Check(model.BindBrowser(tab_id, 61), "bind browser");
  Check(model.UpdateLoading(61, true, false, false), "loading started");

  Check(model.MarkCrashed(61), "crash recorded");
  const TabSnapshot& crashed = RequireTab(model, tab_id);
  Check(crashed.lifecycle == TabLifecycle::kCrashed, "tab marked crashed");
  Check(!crashed.loading, "crash clears loading state");
  Check(model.MarkCrashed(61), "repeated crash callback is idempotent");
  Check(RequireTab(model, tab_id).lifecycle == TabLifecycle::kCrashed,
        "repeated crash keeps crashed state");
  Check(model.Activate(tab_id), "crashed tab stays activatable");

  Check(model.UpdateLoading(61, true, true, false),
        "reload after crash accepted");
  Check(RequireTab(model, tab_id).lifecycle == TabLifecycle::kReady,
        "reload after crash recovers tab");

  Check(!model.MarkCrashed(9999), "unknown browser crash rejected");
}

void UnboundBrowserIdNeverMatchesCreatingTab() {
  TabModel model;
  const TabId tab_id = model.CreateTab().value();
  Check(RequireTab(model, tab_id).lifecycle == TabLifecycle::kCreating,
        "new tab waits for browser binding");

  Check(model.FindByBrowser(0) == nullptr,
        "zero browser id must not match unbound tabs");
  Check(!model.DetachBrowser(0), "zero browser id detach rejected");
  Check(!model.MarkCrashed(0), "zero browser id crash rejected");
  Check(!model.UpdateAddress(0, "about:blank"),
        "zero browser id address rejected");
  Check(!model.UpdateLoading(0, true, false, false),
        "zero browser id loading rejected");
  Check(!model.BeginNavigation(0), "zero browser id navigation rejected");
  Check(RequireTab(model, tab_id).lifecycle == TabLifecycle::kCreating,
        "unbound tab untouched by zero browser id callbacks");
}

void CloseCreatingTabRemovesItImmediately() {
  TabModel model;
  const TabId first = model.CreateTab().value();
  Check(model.BindBrowser(first, 71), "bind first tab");
  const TabId second = model.CreateTab().value();
  Check(model.Activate(first), "activate bound tab");

  Check(model.RequestClose(second), "close creating tab accepted");
  Check(model.Find(second) == nullptr,
        "creating tab without browser is removed immediately");
  Check(model.RequestClose(second), "repeated close of removed tab is stable");
  Check(model.size() == 1, "only bound tab remains");
  Check(model.active_tab() == first, "active tab unchanged");

  const TabId third = model.CreateTab().value();
  Check(model.active_tab() == third, "creating tab becomes active");
  Check(model.RequestClose(third), "close active creating tab accepted");
  Check(model.active_tab() == first,
        "closing active creating tab reactivates remaining tab");
  Check(!model.BindBrowser(third, 72),
        "late async browser callback for removed tab is rejected");
}

void BindBrowserContract() {
  TabModel model;
  const TabId tab_id = model.CreateTab().value();
  Check(!model.BindBrowser(tab_id, 0), "zero browser id bind rejected");
  Check(!model.BindBrowser(tab_id, -3), "negative browser id bind rejected");
  Check(model.BindBrowser(tab_id, 81), "bind accepted");
  Check(RequireTab(model, tab_id).lifecycle == TabLifecycle::kReady,
        "bound tab is ready");
  Check(!model.BindBrowser(tab_id, 82), "rebind of ready tab rejected");
  const TabId other = model.CreateTab().value();
  Check(!model.BindBrowser(other, 81), "duplicate browser id rejected");
  Check(!model.BindBrowser(9999, 83), "unknown tab bind rejected");
  Check(model.FindByBrowser(81) != nullptr &&
            model.FindByBrowser(81)->id == tab_id,
        "browser id lookup finds bound tab");
}

}  // namespace


namespace popup = crayon::browser::cef_shell::window;
using crayon::browser::cef_shell::window::PopupTargetAction;

// CEF-16: popup routing must open user-gesture http/https targets in a new
// tab and fail closed for everything else.
void PopupUserGestureOpensInNewTab() {
  Check(popup::EvaluatePopupTarget("https://example.com/next", true, 0,
                                   false) == PopupTargetAction::kOpenInNewTab,
        "user-gesture https popup must route to a new tab");
  Check(popup::EvaluatePopupTarget("http://127.0.0.1:8080/page", true, 0,
                                   false) == PopupTargetAction::kOpenInNewTab,
        "user-gesture http popup must route to a new tab");
}

void PopupProgrammaticDenied() {
  Check(popup::EvaluatePopupTarget("https://example.com/next", false, 0,
                                   false) == PopupTargetAction::kDeny,
        "programmatic popup must be denied");
}

void PopupSchemeAndShapeMatrix() {
  const char* denied[] = {
      "", "ftp://example.com/x", "file:///D:/doc.md", "javascript:alert(1)",
      "chrome://newtab/", "crayon://mdv/app.html", "https://",
      "https://exa mple.com/x", "https://example.com/a	b",
  };
  for (const char* url : denied) {
    Check(popup::EvaluatePopupTarget(url, true, 0, false) ==
              PopupTargetAction::kDeny,
          url);
  }
  std::string too_long = "https://example.com/" +
                         std::string(popup::kMaxPopupUrlBytes, 'a');
  Check(popup::EvaluatePopupTarget(too_long, true, 0, false) ==
            PopupTargetAction::kDeny,
        "over-length popup URL must be denied");
}

void PopupCapacityDenied() {
  Check(popup::EvaluatePopupTarget("https://example.com/x", true, 4, false) ==
            PopupTargetAction::kDeny,
        "pending popup queue at the per-opener cap must be denied");
  Check(popup::EvaluatePopupTarget("https://example.com/x", true, 0, true) ==
            PopupTargetAction::kDeny,
        "full tab strip must be denied");
}

int main() {
  const std::pair<const char*, void (*)()> cases[] = {
      {"CreateActivateAndOrder", &CreateActivateAndOrder},
      {"CapacityLimit", &CapacityLimit},
      {"DuplicateAndStaleClose", &DuplicateAndStaleClose},
      {"CloseLastTabEmptiesModel", &CloseLastTabEmptiesModel},
      {"ActiveReplacementPrefersNextThenPrevious",
       &ActiveReplacementPrefersNextThenPrevious},
      {"LoadingHistoryAndAddress", &LoadingHistoryAndAddress},
      {"NavigationGeneration", &NavigationGeneration},
      {"ZoomBounds", &ZoomBounds},
      {"CrashDetachAndRecovery", &CrashDetachAndRecovery},
      {"UnboundBrowserIdNeverMatchesCreatingTab",
       &UnboundBrowserIdNeverMatchesCreatingTab},
      {"CloseCreatingTabRemovesItImmediately",
       &CloseCreatingTabRemovesItImmediately},
      {"BindBrowserContract", &BindBrowserContract},
      {"PopupUserGestureOpensInNewTab", &PopupUserGestureOpensInNewTab},
      {"PopupProgrammaticDenied", &PopupProgrammaticDenied},
      {"PopupSchemeAndShapeMatrix", &PopupSchemeAndShapeMatrix},
      {"PopupCapacityDenied", &PopupCapacityDenied},
  };
  for (const auto& test_case : cases) {
    test_case.second();
  }
  return 0;
}
