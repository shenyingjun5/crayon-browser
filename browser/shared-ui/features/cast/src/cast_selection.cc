#include "crayon/browser_cast_view/cast_selection.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace crayon::browser_cast_view {
namespace {

bool Token(std::string_view value) {
  return !value.empty() && value.size() <= kCastSelectionIdBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                  c == ':';
         });
}

// Reject malformed UTF-8 and control/bidi formatting. Text stays plain text;
// neither markup nor an apparently meaningful title is interpreted as an ID.
bool DisplayText(std::string_view text) {
  if (text.size() > kCastSelectionTitleBytes)
    return false;
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    std::uint32_t cp = first;
    unsigned remaining = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      cp = first & 0x1f;
      remaining = 1;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      cp = first & 0x0f;
      remaining = 2;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      cp = first & 7;
      remaining = 3;
      minimum = 0x10000;
    } else if (first >= 0x80)
      return false;
    if (remaining > text.size() - i)
      return false;
    while (remaining > 0) {
      --remaining;
      const auto c = static_cast<unsigned char>(text[i++]);
      if ((c & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (c & 0x3f);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
        cp < 0x20 || (cp >= 0x7f && cp <= 0x9f) || cp == 0x061c ||
        (cp >= 0x200e && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) ||
        (cp >= 0x2066 && cp <= 0x2069))
      return false;
  }
  return true;
}

bool Valid(const CastViewContext &c) {
  return c.browser_session && Token(c.profile_id) && c.tab_id &&
         c.navigation_id && c.generation;
}
bool Valid(const CastMediaChoice &c) {
  return c.ref.instance_id && c.ref.source_revision && DisplayText(c.title);
}
bool Valid(const CastDeviceChoice &c) {
  return Token(c.id) && DisplayText(c.name);
}
bool Page(std::size_t size, std::uint16_t offset, std::uint16_t total) {
  return size <= kCastSelectionPageSize && total <= kCastSelectionCapacity &&
         offset % kCastSelectionPageSize == 0 &&
         (total ? offset < total : offset == 0) &&
         size == std::min<std::size_t>(kCastSelectionPageSize, total - offset);
}
bool Valid(const CastSelectionSnapshot &s) {
  if (!Valid(s.context) || !s.view_revision ||
      !Page(s.media.size(), s.media_offset, s.media_total) ||
      !Page(s.devices.size(), s.device_offset, s.device_total) ||
      s.eligible_count > s.media_total ||
      (s.picker_open && (!s.draft_id || !s.draft_revision)) ||
      (s.session_generation && !*s.session_generation))
    return false;
  const auto visible_eligible =
      std::count_if(s.media.begin(), s.media.end(),
                    [](const auto &m) { return m.selectable; });
  if (visible_eligible > s.eligible_count ||
      s.eligible_count > visible_eligible + s.media_total - s.media.size())
    return false;
  switch (s.phase) {
  case CastDraftPhase::kChoosing:
  case CastDraftPhase::kConnecting:
  case CastDraftPhase::kPreparing:
  case CastDraftPhase::kPrepared:
  case CastDraftPhase::kCommitting:
  case CastDraftPhase::kFailed:
  case CastDraftPhase::kExpired:
    break;
  default:
    return false;
  }
  switch (s.route) {
  case CastSelectionRoute::kNone:
  case CastSelectionRoute::kDirect:
  case CastSelectionRoute::kRelay:
    break;
  default:
    return false;
  }
  for (std::size_t i = 0; i < s.media.size(); ++i) {
    if (!Valid(s.media[i]))
      return false;
    for (std::size_t j = 0; j < i; ++j)
      if (s.media[i].ref.instance_id == s.media[j].ref.instance_id)
        return false;
    if (s.selected_media &&
        s.selected_media->ref.instance_id == s.media[i].ref.instance_id &&
        (!(s.selected_media->ref == s.media[i].ref) ||
         s.selected_media->selectable != s.media[i].selectable ||
         s.selected_media->title != s.media[i].title))
      return false;
  }
  for (std::size_t i = 0; i < s.devices.size(); ++i) {
    if (!Valid(s.devices[i]))
      return false;
    for (std::size_t j = 0; j < i; ++j)
      if (s.devices[i].id == s.devices[j].id)
        return false;
    if (s.selected_device && s.selected_device->id == s.devices[i].id &&
        (s.selected_device->selectable != s.devices[i].selectable ||
         s.selected_device->name != s.devices[i].name))
      return false;
  }
  return (!s.selected_media || (s.media_total && Valid(*s.selected_media))) &&
         (!s.selected_device || (s.device_total && Valid(*s.selected_device)));
}
bool Fresh(std::uint64_t deadline, std::uint64_t now, std::uint64_t maximum) {
  return deadline > now && deadline - now <= maximum;
}

} // namespace

bool CastViewContext::operator==(const CastViewContext &c) const {
  return browser_session == c.browser_session && profile_id == c.profile_id &&
         tab_id == c.tab_id && navigation_id == c.navigation_id &&
         generation == c.generation;
}
bool CastMediaRef::operator==(const CastMediaRef &r) const {
  return instance_id == r.instance_id && source_revision == r.source_revision;
}
void CastSelectionPresentation::BindContext(CastViewContext context) {
  if (context_ && *context_ == context)
    return;
  Clear();
  if (Valid(context))
    context_ = std::move(context);
}
void CastSelectionPresentation::Clear() {
  context_.reset();
  snapshot_.reset();
  last_view_revision_ = sent_draft_id_ = sent_draft_revision_ = 0;
  cancelled_draft_id_ = 0;
}
bool CastSelectionPresentation::Apply(CastSelectionSnapshot snapshot) {
  // Do not let a delayed reply switch contexts or roll back a selection.
  if (!context_ || !(snapshot.context == *context_) ||
      snapshot.view_revision <= last_view_revision_)
    return false;
  last_view_revision_ = snapshot.view_revision;
  if (!Valid(snapshot)) {
    snapshot_.reset();
    return false;
  }
  snapshot_ = std::move(snapshot);
  return true;
}
bool CastSelectionPresentation::EntryEnabled() const {
  return snapshot_ && snapshot_->compatible &&
         (snapshot_->eligible_count || snapshot_->picker_open ||
          snapshot_->session_generation);
}
bool CastSelectionPresentation::Busy() const {
  if (!snapshot_)
    return false;
  const auto &s = *snapshot_;
  return s.phase == CastDraftPhase::kConnecting ||
         s.phase == CastDraftPhase::kPreparing ||
         s.phase == CastDraftPhase::kCommitting ||
         (s.draft_id && sent_draft_id_ == s.draft_id &&
          sent_draft_revision_ == s.draft_revision);
}
bool CastSelectionPresentation::PickerVisible() const {
  return snapshot_ && snapshot_->compatible && snapshot_->picker_open &&
         snapshot_->draft_id != cancelled_draft_id_;
}
bool CastSelectionPresentation::CompleteSelection() const {
  return snapshot_ && snapshot_->selected_media &&
         snapshot_->selected_media->selectable && snapshot_->selected_device &&
         snapshot_->selected_device->selectable;
}
const CastMediaChoice *
CastSelectionPresentation::FindMedia(CastMediaRef ref) const {
  if (!snapshot_)
    return nullptr;
  for (const auto &m : snapshot_->media)
    if (m.ref == ref)
      return &m;
  return nullptr;
}
const char *CastSelectionPresentation::StatusKey(std::uint64_t now) const {
  if (!snapshot_ || !snapshot_->compatible)
    return "cast.selection.unavailable";
  const auto &s = *snapshot_;
  if (s.draft_id && sent_draft_id_ == s.draft_id &&
      sent_draft_revision_ == s.draft_revision)
    return "cast.selection.submitting";
  if (s.phase == CastDraftPhase::kPrepared &&
      !Fresh(s.prepared_until_ms, now, kCastPreparationLifetimeMs))
    return "cast.selection.expired";
  switch (s.phase) {
  case CastDraftPhase::kConnecting:
    return "cast.selection.connecting";
  case CastDraftPhase::kPreparing:
    return "cast.planning";
  case CastDraftPhase::kCommitting:
    return "cast.selection.submitting";
  case CastDraftPhase::kFailed:
    return "cast.rejected";
  case CastDraftPhase::kExpired:
    return "cast.selection.expired";
  case CastDraftPhase::kPrepared:
    return "cast.selection.ready";
  case CastDraftPhase::kChoosing:
    break;
  }
  if (!s.eligible_count)
    return "cast.disabled";
  if (!s.selected_media || !s.selected_media->selectable)
    return "cast.selection.choose_video";
  if (!s.selected_device || !s.selected_device->selectable)
    return "cast.selection.choose_device";
  return s.device_connected ? "cast.selection.connected"
                            : "cast.selection.device_selected";
}
CastSelectionIntent
CastSelectionPresentation::Intent(CastIntentKind kind) const {
  CastSelectionIntent i;
  i.kind = kind;
  if (snapshot_) {
    i.context = snapshot_->context;
    i.view_revision = snapshot_->view_revision;
    i.draft_id = snapshot_->draft_id;
    i.draft_revision = snapshot_->draft_revision;
    i.session_generation = snapshot_->session_generation;
  }
  return i;
}
bool CastSelectionPresentation::Allows(const CastSelectionIntent &i,
                                       std::uint64_t now) const {
  if (!snapshot_ || !snapshot_->compatible)
    return false;
  const auto &s = *snapshot_;
  if (!(i.context == s.context) || i.view_revision != s.view_revision ||
      i.draft_id != s.draft_id || i.draft_revision != s.draft_revision ||
      i.session_generation != s.session_generation)
    return false;
  // Closed payload shapes prevent ignored fields from acquiring future meaning.
  if ((i.kind != CastIntentKind::kSelectMedia &&
       i.kind != CastIntentKind::kOpenForMedia && i.media) ||
      (i.kind != CastIntentKind::kSelectDevice && !i.device_id.empty()) ||
      (i.kind != CastIntentKind::kLookupCode && !i.cast_code.empty()) ||
      (i.kind != CastIntentKind::kMediaPage &&
       i.kind != CastIntentKind::kDevicePage && i.page_offset))
    return false;
  switch (i.kind) {
  case CastIntentKind::kStop:
    return s.session_generation.has_value();
  case CastIntentKind::kPause:
    return s.session_generation && !s.playback_paused;
  case CastIntentKind::kResume:
    return s.session_generation && s.playback_paused;
  case CastIntentKind::kOpen:
    return EntryEnabled() && !Busy();
  case CastIntentKind::kOpenForMedia: {
    const auto *m = i.media ? FindMedia(*i.media) : nullptr;
    return m && m->selectable && !Busy();
  }
  case CastIntentKind::kCancel:
    return PickerVisible();
  default:
    break;
  }
  if (!PickerVisible() || Busy())
    return false;
  switch (i.kind) {
  case CastIntentKind::kSelectMedia: {
    const auto *m = i.media ? FindMedia(*i.media) : nullptr;
    return m && m->selectable;
  }
  case CastIntentKind::kSelectDevice:
    return std::any_of(s.devices.begin(), s.devices.end(), [&](const auto &d) {
      return d.id == i.device_id && d.selectable;
    });
  case CastIntentKind::kMediaPage:
    return i.page_offset < s.media_total &&
           i.page_offset % kCastSelectionPageSize == 0;
  case CastIntentKind::kDevicePage:
    return i.page_offset < s.device_total &&
           i.page_offset % kCastSelectionPageSize == 0;
  case CastIntentKind::kRefreshDevices:
    return true;
  case CastIntentKind::kLookupCode:
    return !i.cast_code.empty() &&
           i.cast_code.size() <= kCastSelectionCodeBytes &&
           std::all_of(
               i.cast_code.begin(), i.cast_code.end(), [](unsigned char c) {
                 return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z');
               });
  case CastIntentKind::kConnectDevice:
    return s.selected_device && s.selected_device->selectable &&
           !s.device_connected;
  case CastIntentKind::kPrepare:
    return CompleteSelection();
  case CastIntentKind::kConfirmReplacement:
    return CompleteSelection() && s.replacement_confirmation_required;
  case CastIntentKind::kCommit:
    return CompleteSelection() && s.phase == CastDraftPhase::kPrepared &&
           s.route != CastSelectionRoute::kNone &&
           !s.replacement_confirmation_required &&
           Fresh(s.prepared_until_ms, now, kCastPreparationLifetimeMs);
  default:
    return false;
  }
}
std::optional<CastSelectionIntent>
CastSelectionPresentation::TakeIntent(CastSelectionIntent i,
                                      std::uint64_t now) {
  if (!Allows(i, now))
    return std::nullopt;
  if (i.kind == CastIntentKind::kCommit) {
    sent_draft_id_ = i.draft_id;
    sent_draft_revision_ = i.draft_revision;
  }
  if (i.kind == CastIntentKind::kCancel)
    cancelled_draft_id_ = i.draft_id;
  return i;
}
std::optional<CastOverlayBounds>
CastSelectionPresentation::PlaceOverlay(const CastVideoAnchor &a,
                                        int viewport_width, int viewport_height,
                                        std::uint64_t now) const {
  constexpr int kMaxViewportDip = 32768;
  const auto *media = FindMedia(a.media);
  if (!snapshot_ || !snapshot_->compatible || Busy() || !a.supported ||
      !media || !media->selectable || !(a.context == snapshot_->context) ||
      a.view_revision != snapshot_->view_revision ||
      !Fresh(a.expires_at_ms, now, kCastGeometryLifetimeMs) ||
      viewport_width <= 0 || viewport_height <= 0 ||
      viewport_width > kMaxViewportDip || viewport_height > kMaxViewportDip ||
      !std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.width) ||
      !std::isfinite(a.height) || a.width <= 0 || a.height <= 0)
    return std::nullopt;
  const double left = std::ceil(std::max(0.0, a.x)),
               top = std::ceil(std::max(0.0, a.y));
  const double right =
      std::floor(std::min(static_cast<double>(viewport_width), a.x + a.width));
  const double bottom = std::floor(
      std::min(static_cast<double>(viewport_height), a.y + a.height));
  if (right - left < kCastOverlayWidthDip + 2 * kCastOverlayInsetDip ||
      bottom - top < kCastOverlayHeightDip + 2 * kCastOverlayInsetDip)
    return std::nullopt;
  return CastOverlayBounds{static_cast<int>(std::floor(right)) -
                               kCastOverlayWidthDip - kCastOverlayInsetDip,
                           static_cast<int>(std::ceil(top)) +
                               kCastOverlayInsetDip,
                           kCastOverlayWidthDip, kCastOverlayHeightDip};
}

} // namespace crayon::browser_cast_view
