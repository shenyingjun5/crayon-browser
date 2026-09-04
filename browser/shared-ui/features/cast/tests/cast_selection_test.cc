#include "crayon/browser_cast_view/cast_selection.h"

#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace crayon::browser_cast_view;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __LINE__ << ": " << #condition << '\n';                     \
      return false;                                                            \
    }                                                                          \
  } while (false)

CastSelectionSnapshot Snapshot() {
  CastSelectionSnapshot s;
  s.context = {1, "profile-1", 2, 3, 4};
  s.view_revision = 1;
  s.compatible = true;
  s.picker_open = true;
  s.draft_id = 5;
  s.draft_revision = 6;
  s.media = {{{10, 1}, "同名视频", true}, {{20, 1}, "同名视频", true}};
  s.media_total = s.eligible_count = 2;
  s.devices = {{"device-1", "客厅", true}, {"device-2", "客厅", false}};
  s.device_total = 2;
  return s;
}
CastSelectionPresentation Presentation(const CastSelectionSnapshot &s) {
  CastSelectionPresentation p;
  p.BindContext(s.context);
  p.Apply(s);
  return p;
}
CastSelectionSnapshot Prepared() {
  auto s = Snapshot();
  s.selected_media = s.media[0];
  s.selected_device = s.devices[0];
  s.device_connected = true;
  s.phase = CastDraftPhase::kPrepared;
  s.route = CastSelectionRoute::kDirect;
  s.prepared_until_ms = 200;
  return s;
}
bool ExplicitSelection() {
  auto s = Snapshot();
  auto p = Presentation(s);
  CHECK(p.EntryEnabled() && p.PickerVisible());
  CHECK(!p.snapshot()->selected_media && !p.snapshot()->selected_device);
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  auto choose = p.Intent(CastIntentKind::kSelectMedia);
  choose.media = s.media[1].ref;
  CHECK(p.TakeIntent(choose, 100));
  CHECK(!p.snapshot()->selected_media); // Dispatch never impersonates owner
                                        // acknowledgement.
  s.selected_media = s.media[1];
  ++s.view_revision;
  ++s.draft_revision;
  CHECK(p.Apply(s));
  CHECK(!p.TakeIntent(choose, 100));
  auto device = p.Intent(CastIntentKind::kSelectDevice);
  device.device_id = "device-2";
  CHECK(!p.TakeIntent(device, 100));
  device.device_id = "device-1";
  CHECK(p.TakeIntent(device, 100));
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kPrepare), 100));
  s.selected_device = s.devices[0];
  ++s.view_revision;
  CHECK(p.Apply(s));
  CHECK(p.TakeIntent(p.Intent(CastIntentKind::kPrepare), 100));
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  std::swap(s.media[0], s.media[1]);
  ++s.view_revision;
  CHECK(p.Apply(s));
  CHECK(p.snapshot()->selected_media->ref.instance_id == 20);
  auto overlay = p.Intent(CastIntentKind::kOpenForMedia);
  overlay.media = s.media[1].ref;
  CHECK(p.TakeIntent(overlay, 100)->kind == CastIntentKind::kOpenForMedia);
  CHECK(p.snapshot()->selected_media->ref.instance_id == 20);
  return true;
}
bool CommitCancelAndSession() {
  auto s = Prepared();
  auto p = Presentation(s);
  CHECK(p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  CHECK(p.Busy());
  CHECK(std::string(p.StatusKey(100)) == "cast.selection.submitting");
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  ++s.view_revision;
  CHECK(p.Apply(s));
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  CHECK(p.TakeIntent(p.Intent(CastIntentKind::kCancel), 100));
  CHECK(!p.PickerVisible());
  ++s.view_revision;
  ++s.draft_revision;
  CHECK(p.Apply(s));
  CHECK(!p.PickerVisible()); // Late acknowledgement cannot resurrect cancelled
                             // draft.
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  ++s.view_revision;
  ++s.draft_id;
  CHECK(p.Apply(s) && p.PickerVisible());
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 200));
  CHECK(std::string(p.StatusKey(200)) == "cast.selection.expired");
  s.replacement_confirmation_required = true;
  ++s.view_revision;
  CHECK(p.Apply(s));
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kCommit), 100));
  CHECK(p.TakeIntent(p.Intent(CastIntentKind::kConfirmReplacement), 100));
  s = Snapshot();
  s.media.clear();
  s.media_total = s.eligible_count = 0;
  s.picker_open = false;
  p = Presentation(s);
  CHECK(!p.EntryEnabled());
  s.session_generation = 7;
  ++s.view_revision;
  CHECK(p.Apply(s) && p.EntryEnabled());
  CHECK(p.TakeIntent(p.Intent(CastIntentKind::kStop), 100));
  CHECK(p.TakeIntent(p.Intent(CastIntentKind::kPause), 100));
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kResume), 100));
  auto stop = p.Intent(CastIntentKind::kStop);
  stop.session_generation = 6;
  CHECK(!p.TakeIntent(stop, 100));
  return true;
}
bool ContextAndPayload() {
  auto s = Prepared();
  auto p = Presentation(s);
  auto intent = p.Intent(CastIntentKind::kCommit);
  intent.device_id = "extra";
  CHECK(!p.TakeIntent(intent, 100));
  intent = p.Intent(CastIntentKind::kSelectMedia);
  intent.media = CastMediaRef{10, 2};
  CHECK(!p.TakeIntent(intent, 100));
  CHECK(!p.Apply(s));
  ++s.context.navigation_id;
  ++s.view_revision;
  CHECK(!p.Apply(s));
  p.BindContext(s.context);
  CHECK(!p.EntryEnabled() && !p.snapshot());
  CHECK(p.Apply(s));
  s.compatible = false;
  ++s.view_revision;
  CHECK(p.Apply(s) && !p.EntryEnabled());
  CHECK(!p.TakeIntent(p.Intent(CastIntentKind::kOpen), 100));
  p = Presentation(Snapshot());
  auto code = p.Intent(CastIntentKind::kLookupCode);
  code.cast_code = "AB12";
  CHECK(p.TakeIntent(code, 100));
  code.cast_code = "https://x";
  CHECK(!p.TakeIntent(code, 100));
  code.cast_code = std::string(kCastSelectionCodeBytes + 1, 'A');
  CHECK(!p.TakeIntent(code, 100));
  return true;
}
bool BoundsAndPages() {
  auto rejected = [](CastSelectionSnapshot s) {
    CastSelectionPresentation p;
    p.BindContext(s.context);
    return !p.Apply(s) && !p.EntryEnabled();
  };
  for (const auto &text :
       {std::string("bad\n"), std::string("\xc0\xaf"),
        std::string("\xe2\x80\xae"), std::string("\xed\xa0\x80"),
        std::string(129, 'a'), std::string("x\0y", 3)}) {
    auto s = Snapshot();
    s.media[0].title = text;
    CHECK(rejected(s));
  }
  auto s = Snapshot();
  s.media[0].title = "<b>纯文本</b>";
  CHECK(Presentation(s).EntryEnabled());
  s.media[1].ref = s.media[0].ref;
  CHECK(rejected(s));
  s = Snapshot();
  s.eligible_count = 1;
  CHECK(rejected(s));
  s = Prepared();
  ++s.media[0].ref.source_revision;
  CHECK(rejected(s));
  s = Snapshot();
  s.device_total = 257;
  CHECK(rejected(s));
  s = Snapshot();
  s.media_total = s.eligible_count = 256;
  s.media.clear();
  for (std::uint64_t i = 0; i < kCastSelectionPageSize; ++i)
    s.media.push_back({{i + 1, 1}, "video", true});
  auto p = Presentation(s);
  CHECK(p.EntryEnabled());
  auto page = p.Intent(CastIntentKind::kMediaPage);
  page.page_offset = 240;
  CHECK(p.TakeIntent(page, 100));
  page.page_offset = 256;
  CHECK(!p.TakeIntent(page, 100));
  page.page_offset = 1;
  CHECK(!p.TakeIntent(page, 100));
  s.selected_media = s.media[0];
  s.media_offset = 16;
  for (auto &m : s.media)
    m.ref.instance_id += 16;
  ++s.view_revision;
  CHECK(p.Apply(s) && p.snapshot()->selected_media->ref.instance_id == 1);
  s.media.push_back({{99, 1}, "extra", true});
  ++s.view_revision;
  CHECK(!p.Apply(s) && !p.EntryEnabled());
  return true;
}
bool OverlayGeometry() {
  auto s = Snapshot();
  auto p = Presentation(s);
  CastVideoAnchor a{
      s.context, s.view_revision, s.media[0].ref, 150, true, 10, 20, 640, 360};
  auto bounds = p.PlaceOverlay(a, 800, 600, 100);
  CHECK(bounds && bounds->x == 546 && bounds->y == 28);
  CHECK(!p.PlaceOverlay(a, 800, 600, 150));
  a.expires_at_ms = 1000;
  CHECK(!p.PlaceOverlay(a, 800, 600, 100));
  a.expires_at_ms = 150;
  a.supported = false;
  CHECK(!p.PlaceOverlay(a, 800, 600, 100));
  a.supported = true;
  ++a.media.source_revision;
  CHECK(!p.PlaceOverlay(a, 800, 600, 100));
  --a.media.source_revision;
  a.width = 30;
  CHECK(!p.PlaceOverlay(a, 800, 600, 100));
  a.width = 640;
  a.x = -300;
  bounds = p.PlaceOverlay(a, 300, 200, 100);
  CHECK(bounds && bounds->x >= 0 && bounds->x + bounds->width <= 300);
  a.x = std::numeric_limits<double>::quiet_NaN();
  CHECK(!p.PlaceOverlay(a, 800, 600, 100));
  a.x = 10;
  ++a.view_revision;
  CHECK(!p.PlaceOverlay(a, 800, 600, 100));
  return true;
}
} // namespace
int main() {
  if (!ExplicitSelection() || !CommitCancelAndSession() ||
      !ContextAndPayload() || !BoundsAndPages() || !OverlayGeometry())
    return 1;
  std::cout << "cast_selection: 5 groups passed\n";
  return 0;
}
