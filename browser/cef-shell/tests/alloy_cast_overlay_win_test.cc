#include "windows/alloy_cast_overlay_win.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

namespace {

namespace cast_view = ::crayon::browser_cast_view;
using ::crayon::browser::cef_shell::windows::AlloyCastOverlayObservation;
using ::crayon::browser::cef_shell::windows::AlloyCastOverlayWin;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition \
                << std::endl;                                                \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (false)

AlloyCastOverlayObservation Observation(
    const cast_view::CastViewContext& context, std::uint64_t revision,
    std::uint64_t expires_at = 1500) {
  AlloyCastOverlayObservation value;
  value.anchor.context = context;
  value.anchor.view_revision = revision;
  value.anchor.media = {10, 1};
  value.anchor.expires_at_ms = expires_at;
  value.anchor.supported = true;
  value.anchor.x = 100;
  value.anchor.y = 100;
  value.anchor.width = 400;
  value.anchor.height = 300;
  value.viewport_width = 800;
  value.viewport_height = 600;
  return value;
}

}  // namespace

int main() {
  HWND root = CreateWindowExW(0, L"STATIC", L"Alloy overlay root",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 840,
                              660, nullptr, nullptr, GetModuleHandleW(nullptr),
                              nullptr);
  CHECK(root);
  HWND browser = CreateWindowExW(0, L"STATIC", L"Alloy browser child",
                                 WS_CHILD | WS_VISIBLE, 10, 10, 800, 600, root,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
  CHECK(browser);

  std::uint64_t now = 1000;
  std::optional<cast_view::CastMediaRef> activated;
  AlloyCastOverlayWin overlay(
      L"投屏此视频", [&] { return now; },
      [&](cast_view::CastMediaRef media) {
        activated = media;
        return true;
      });
  CHECK(overlay.Attach(root, browser));
  const cast_view::CastViewContext context{1, "profile", 2, 3, 4};
  overlay.BindContext(context);
  cast_view::CastSelectionSnapshot snapshot;
  snapshot.context = context;
  snapshot.view_revision = 7;
  snapshot.compatible = true;
  snapshot.eligible_count = 1;
  snapshot.media_total = 1;
  snapshot.media.push_back({{10, 1}, "Video", true});
  CHECK(overlay.Apply(snapshot));
  SendMessageW(root, WM_ACTIVATE, WA_ACTIVE, 0);

  overlay.SetObservations({Observation(context, 7)});
  CHECK(overlay.visible_count() == 1);
  HWND button = GetDlgItem(root, AlloyCastOverlayWin::kFirstControlId);
  CHECK(button && IsWindowVisible(button));
  RECT bounds{};
  CHECK(GetWindowRect(button, &bounds));
  POINT expected{406, 118};
  CHECK(ClientToScreen(root, &expected));
  CHECK(std::abs(bounds.left - expected.x) <= 2);
  CHECK(std::abs(bounds.top - expected.y) <= 2);
  SendMessageW(button, BM_CLICK, 0, 0);
  CHECK(activated && activated->instance_id == 10 &&
        activated->source_revision == 1);
  CHECK(overlay.visible_count() == 0);

  auto unsupported = Observation(context, 7);
  unsupported.anchor.supported = false;
  overlay.SetObservations({unsupported});
  CHECK(overlay.visible_count() == 0);
  overlay.SetObservations({Observation(context, 7, 900)});
  CHECK(overlay.visible_count() == 0);
  auto duplicate = Observation(context, 7);
  overlay.SetObservations({duplicate, duplicate});
  CHECK(overlay.visible_count() == 0);
  overlay.SetObservations({Observation(context, 8)});
  CHECK(overlay.visible_count() == 0);

  overlay.SetObservations({Observation(context, 7)});
  CHECK(overlay.visible_count() == 1);
  SendMessageW(root, WM_ACTIVATE, WA_INACTIVE, 0);
  CHECK(overlay.visible_count() == 0);
  SendMessageW(root, WM_ACTIVATE, WA_ACTIVE, 0);
  CHECK(overlay.visible_count() == 1);
  now = 1600;
  overlay.Tick();
  CHECK(overlay.visible_count() == 0);

  overlay.Detach();
  CHECK(!GetDlgItem(root, AlloyCastOverlayWin::kFirstControlId));
  DestroyWindow(root);
  return EXIT_SUCCESS;
}
